# render.debug3d — 3D Debug Drawing

## Problem

Melody has `render.draw` for retained 2D draw contexts (rects, lines → GPU vertex buffers) but no equivalent for 3D debug primitives. When working with 3D systems (skeletons, IK chains, collision volumes, physics particles, spline paths, normals, coordinate frames), you need to throw debug geometry into the scene quickly without building full mesh infrastructure for each system.

## What This Module Provides

- Immediate-mode 3D debug drawing API: queue primitives, they render this frame and are discarded
- Primitives: points, lines, triangles, wireframe boxes, wireframe spheres, arrows, coordinate axes, circles, grids
- Per-primitive color (vertex-colored, no textures, no lighting)
- Depth testing on/off (in-scene vs overlay)
- Optional duration: persist primitives for N seconds
- Integration with the render graph as a pass

## Inspiration

- **Animation-Engine**: `src/Graphics/Systems/DebugRenderer.h` — static draw functions for points, segments, AABBs, triangles, skeletons, IK chains, grids, curves
- General: UE DrawDebugLine, Bullet btIDebugDraw

## File Layout

```
render.debug3d.h       — main interface (draw API, context type)
render.debug3d.fwd.h   — forward declarations
render.debug3d.c       — buffer management, primitive generation, render pass
```

---

## Types

### Vertex format

```c
typedef struct {
    Mel_Vec3 position;
    Mel_Vec4 color;
} Mel_Debug3D_Vertex;
```

Position + color. No UVs, no normals. 28 bytes per vertex.

### Mel_Debug3D_Ctx

```c
typedef struct {
    Mel_Debug3D_Vertex* line_vertices;
    u32 line_vertex_count;
    u32 line_vertex_capacity;

    Mel_Debug3D_Vertex* tri_vertices;
    u32 tri_vertex_count;
    u32 tri_vertex_capacity;

    Mel_Debug3D_Vertex* overlay_line_vertices;
    u32 overlay_line_vertex_count;

    Mel_Debug3D_Vertex* overlay_tri_vertices;
    u32 overlay_tri_vertex_count;

    Mel_Debug3D_Gpu_Frame gpu_frames[MEL_MAX_FRAMES_IN_FLIGHT];
    u32 gpu_frame_index;

    Mel_Gpu_Pipeline line_pipeline;
    Mel_Gpu_Pipeline line_overlay_pipeline;
    Mel_Gpu_Pipeline tri_pipeline;
    Mel_Gpu_Pipeline tri_overlay_pipeline;

    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
} Mel_Debug3D_Ctx;
```

Four separate vertex streams:
- **Lines (depth-tested)**: in-scene wireframe geometry
- **Lines (overlay)**: drawn on top, no depth test
- **Triangles (depth-tested)**: filled debug shapes in-scene
- **Triangles (overlay)**: filled shapes on top

Each stream maps to a different pipeline state (depth test on/off, primitive topology line/triangle). This means 2-4 draw calls per frame total, regardless of how many primitives are queued.

### GPU frame

```c
typedef struct {
    Mel_Gpu_Buffer line_buffer;
    Mel_Gpu_Buffer tri_buffer;
    Mel_Gpu_Buffer overlay_line_buffer;
    Mel_Gpu_Buffer overlay_tri_buffer;
} Mel_Debug3D_Gpu_Frame;
```

CPU_TO_GPU buffers, cycled per frame like the sprite pass and mesh pass.

---

## API

### Lifecycle

```c
typedef struct {
    Mel_Gpu_Device* dev;
    VkFormat color_format;
    VkFormat depth_format;
    u32 max_line_vertices;
    u32 max_tri_vertices;
    const Mel_Alloc* alloc;
} Mel_Debug3D_Init_Opt;

void mel_debug3d_init_opt(Mel_Debug3D_Ctx* ctx, Mel_Debug3D_Init_Opt opt);
#define mel_debug3d_init(ctx, ...) mel_debug3d_init_opt((ctx), (Mel_Debug3D_Init_Opt){__VA_ARGS__})

void mel_debug3d_shutdown(Mel_Debug3D_Ctx* ctx);
```

`max_line_vertices` and `max_tri_vertices` define the budget. 64K each is a reasonable default. If the budget is exceeded in a frame, excess primitives are silently dropped (with an assert in debug builds).

### Frame cycle

```c
void mel_debug3d_begin(Mel_Debug3D_Ctx* ctx);
void mel_debug3d_end(Mel_Debug3D_Ctx* ctx);
```

`begin` clears the vertex buffers and advances `gpu_frame_index`. `end` uploads CPU vertices to the GPU buffer. Call `begin` at frame start, queue primitives, call `end` before the render pass executes.

### Drawing primitives

All draw functions take an optional `bool overlay` parameter (defaults to false = depth-tested).

```c
typedef struct {
    bool overlay;
} Mel_Debug3D_Draw_Opt;

void mel_debug3d_point_opt(Mel_Debug3D_Ctx* ctx, Mel_Vec3 pos, Mel_Vec4 color, f32 size, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_point(ctx, pos, color, size, ...) \
    mel_debug3d_point_opt((ctx), (pos), (color), (size), (Mel_Debug3D_Draw_Opt){__VA_ARGS__})

void mel_debug3d_line_opt(Mel_Debug3D_Ctx* ctx, Mel_Vec3 from, Mel_Vec3 to, Mel_Vec4 color, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_line(ctx, from, to, color, ...) \
    mel_debug3d_line_opt((ctx), (from), (to), (color), (Mel_Debug3D_Draw_Opt){__VA_ARGS__})

void mel_debug3d_triangle_opt(Mel_Debug3D_Ctx* ctx, Mel_Vec3 v0, Mel_Vec3 v1, Mel_Vec3 v2, Mel_Vec4 color, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_triangle(ctx, v0, v1, v2, color, ...) \
    mel_debug3d_triangle_opt((ctx), (v0), (v1), (v2), (color), (Mel_Debug3D_Draw_Opt){__VA_ARGS__})
```

### Compound primitives (built from lines/triangles)

```c
void mel_debug3d_aabb_opt(Mel_Debug3D_Ctx* ctx, Mel_Vec3 min, Mel_Vec3 max, Mel_Vec4 color, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_aabb(ctx, min, max, color, ...) \
    mel_debug3d_aabb_opt((ctx), (min), (max), (color), (Mel_Debug3D_Draw_Opt){__VA_ARGS__})

void mel_debug3d_sphere_opt(Mel_Debug3D_Ctx* ctx, Mel_Vec3 center, f32 radius, Mel_Vec4 color, u32 segments, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_sphere(ctx, center, radius, color, ...) \
    mel_debug3d_sphere_opt((ctx), (center), (radius), (color), 16, (Mel_Debug3D_Draw_Opt){__VA_ARGS__})

void mel_debug3d_arrow_opt(Mel_Debug3D_Ctx* ctx, Mel_Vec3 from, Mel_Vec3 to, Mel_Vec4 color, f32 head_size, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_arrow(ctx, from, to, color, ...) \
    mel_debug3d_arrow_opt((ctx), (from), (to), (color), 0.1f, (Mel_Debug3D_Draw_Opt){__VA_ARGS__})

void mel_debug3d_axes_opt(Mel_Debug3D_Ctx* ctx, Mel_Vec3 pos, Mel_Mat4 rotation, f32 length, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_axes(ctx, pos, rotation, length, ...) \
    mel_debug3d_axes_opt((ctx), (pos), (rotation), (length), (Mel_Debug3D_Draw_Opt){__VA_ARGS__})

void mel_debug3d_circle_opt(Mel_Debug3D_Ctx* ctx, Mel_Vec3 center, Mel_Vec3 normal, f32 radius, Mel_Vec4 color, u32 segments, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_circle(ctx, center, normal, radius, color, ...) \
    mel_debug3d_circle_opt((ctx), (center), (normal), (radius), (color), 32, (Mel_Debug3D_Draw_Opt){__VA_ARGS__})

void mel_debug3d_grid_opt(Mel_Debug3D_Ctx* ctx, f32 x_size, f32 z_size, u32 x_subdivisions, u32 z_subdivisions, Mel_Vec4 color, Mel_Debug3D_Draw_Opt opt);
#define mel_debug3d_grid(ctx, x_size, z_size, x_sub, z_sub, color, ...) \
    mel_debug3d_grid_opt((ctx), (x_size), (z_size), (x_sub), (z_sub), (color), (Mel_Debug3D_Draw_Opt){__VA_ARGS__})
```

These are convenience functions that expand into multiple line/triangle draws. AABB = 12 lines. Sphere = rings of line segments. Arrow = line + small cone. Axes = 3 arrows (RGB for XYZ). Grid = NxM lines.

### Render pass execution

```c
void mel_debug3d_execute(Mel_Render_Pass_Ctx* ctx);
```

Registered as a render graph pass. Draws all four streams (lines, tris, overlay lines, overlay tris) using their respective pipelines. The pass needs read access to the depth buffer (for depth-tested draws) and writes to the color target.

---

## Render Graph Integration

```c
mel_render_graph_add_pass(graph, S8("debug3d"),
    .fn = mel_debug3d_execute,
    .user = &debug3d_ctx,
    .write_targets = MEL_WRITE_TARGETS(
        { .target = &color_target, .load_op = VK_ATTACHMENT_LOAD_OP_LOAD }
    ),
    .read_targets = MEL_TARGETS(&depth_target),
    .camera = &camera);
```

Runs after the main 3D pass (loads existing color, reads depth). The depth-tested pipelines use the existing depth buffer for testing but don't write to it. Overlay pipelines disable depth test entirely.

---

## Design Decisions

**Why four separate streams instead of per-primitive depth flags?**
Sorting by depth test vs overlay requires different pipeline state. Grouping by pipeline state means we can draw each group in a single draw call. The alternative (per-primitive flags) would require sorting or multiple passes anyway.

**Why no index buffer?**
Debug geometry is ephemeral and varied — there's no reuse across primitives. Lines are 2 vertices each, triangles are 3. Index buffers add complexity without benefit here.

**Why points are drawn as small crosses (lines) instead of GPU points?**
Vulkan point size support is inconsistent across drivers. Small axis-aligned crosses made of 3 short lines are universally supported and more visible in 3D space.

**Why silently drop excess instead of assert?**
In debug drawing, it's better to see *some* primitives than to crash. But we assert in debug builds so you know the budget was exceeded.

**Why no text labels?**
3D text (billboarded at a world position) requires font rendering infrastructure. `render.draw` already has text rendering for 2D. We could project a 3D point to screen space and use the 2D text system instead of duplicating font rendering in the debug pass. Worth considering but out of scope for v1.

---

## Improvements Over Animation-Engine

1. Proper GPU buffer management (Vulkan, cycled per frame) instead of immediate OpenGL calls
2. Depth-tested and overlay as explicit modes instead of everything overlaid
3. Render graph integration instead of ad-hoc draw calls scattered through the frame
4. No static state / global variables — context struct owns everything
5. Triangle stream for filled shapes (they only had wireframe)
6. Budget system instead of unbounded allocations

---

## Open Questions for Gabbo

1. **Timed persistence**: should we support `mel_debug3d_line(..., .duration = 2.0f)` that keeps a primitive visible for N seconds? Useful for "flash on event" debugging. Requires a persistent buffer alongside the per-frame buffer, with timestamps for cleanup. Adds complexity.

2. **Line width**: Vulkan only guarantees width=1. Should we accept hairlines, or implement screen-space line expansion (geometry shader / mesh shader approach)? Hairlines are fine for most debug use.

3. **Points**: the "small cross" approach works but is more expensive than hardware points (6 vertices instead of 1). Should we try point primitives first and fall back if not supported?

4. **Filled AABB / filled sphere**: the current API has wireframe box and wireframe sphere. Should we add filled versions? Filled sphere requires generating triangle mesh (icosphere or UV sphere). Filled box is 12 triangles.

5. **Integration with other modules**: should domain-specific helpers (draw_skeleton, draw_ik_chain, draw_collider) live in render.debug3d or in their respective modules (anim.ik, etc.)? I lean toward the respective modules calling the debug3d API — keeps debug3d generic.
