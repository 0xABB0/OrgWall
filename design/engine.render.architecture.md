# Rendering Architecture

This document describes Melody's rendering architecture from first principles.

Inspired heavily by Blender's draw system (DRW), adapted for Melody's
vision: runtime-extensible, GPU-driven by default, multi-window with
independent cadences, graceful degradation across all platforms.

## Blender Patterns We Steal

- DrawEngine vtable lifecycle
- draw::Manager (handle-indexed GPU storage buffers for per-object data)
- View-agnostic passes (same pass submitted against multiple views)
- GPU-driven visibility (compute shader culling, bitfield output)
- Per-viewport engine instances (not singletons)

## Where We Diverge From Blender

- Runtime registration of pipelines, not compile-time
- Render Source abstraction (not tied to ECS or depsgraph)
- Delta-sync model (only upload what changed, not full scene per frame)
- Independent cadences per window
- Explicit context passing, not thread-local singleton
- Handles everywhere, minimal raw pointers

---

## Concepts

### 1. Render Source

The rendering system does not care WHERE render objects come from.
It operates on a persistent object database (the Manager).
A Source is responsible for keeping that database in sync with
whatever data structure the user works with.

A Source has one function:

```c
typedef struct Mel_Render_Source_Type {
    str8  name;
    void  (*sync)(Mel_Render_Source* self, Mel_Render_Manager* mgr);
    usize instance_size;
} Mel_Render_Source_Type;
```

`sync` is called once per frame (when at least one view using this source
is active). It tells the Manager what changed since last frame: new objects,
removed objects, modified transforms/materials. Delta only. No full uploads.

The engine provides built-in sources:

**ECS Source** (zero-effort default):

```c
Mel_Render_Source* src = mel_source_ecs(game_world);
```

Observes ECS queries for entities with visual components (Mel_Sprite,
Mel_Mesh, Mel_Light, etc.). Automatically detects added/removed/modified
entities and syncs deltas into the Manager.

**Manual Source** (for things outside ECS):

```c
Mel_Render_Source* src = mel_source_manual_create();

// Each frame, in the sim tick:
mel_source_manual_set_transform(src, handle, new_transform);
mel_source_manual_add(src, &(Mel_Render_Object_Desc){ ... });
mel_source_manual_remove(src, handle);
```

For particle systems, procedural geometry, debug drawing, anything
that doesn't fit in ECS.

**Composite Source** (multiple sources feeding one view):

```c
Mel_Render_Source* combined = mel_source_composite_create();
mel_source_composite_add(combined, mel_source_ecs(game_world));
mel_source_composite_add(combined, particle_source);
mel_source_composite_add(combined, debug_draw_source);
```

**Custom Source** (user-defined sync):

```c
Mel_Render_Source_Type octree_source_type = {
    .name = S8("octree_source"),
    .sync = octree_sync,
    .instance_size = sizeof(Octree_Source_Data),
};
```

The user implements `sync` to delta-update the Manager from whatever
data structure they want.

---

### 2. View

The central abstraction. A View connects:

- A **Source** (what to render)
- A **Camera** (from where)
- A **Target** (where pixels go)
- A **Pipeline** (how to render)

```c
Mel_View* view = mel_view_create(&(Mel_View_Desc){
    .source   = mel_source_ecs(game_world),
    .camera   = main_camera,
    .target   = mel_target_from_window(window),
    .pipeline = S8("default_2d"),
    .priority = 0,
});
```

Each View gets its own pipeline instance. Two views using "default_3d"
have two separate instances with independent internal state. This is
how multi-window and split-screen work without shared mutable state
(pattern from Blender's per-viewport engine instances).

Pipeline defaults to "default_2d" when not specified.

**View configurations:**

Simple 2D game:
  1 source (ECS), 1 view, 1 camera, target = swapchain

Split screen:
  1 source (ECS), 2 views, 2 cameras (different positions),
  same window target but different viewport rects

VR:
  1 source, 1 view with stereo camera,
  target = array image (2 layers, multi-view hardware)

Editor:
  1 ECS source for game world:
  - view A: game camera -> game window (pipeline: "default_2d")
  - view B: editor camera -> editor window (pipeline: "editor_3d")
  - view C: material preview -> small panel (pipeline: "unlit")

Multiple sources on one target:
  Composite source (ECS + particles + debug) -> 1 view -> window

Multiple views composited:
  View 1: game (priority 0, fullscreen viewport)
  View 2: UI (priority 1, fullscreen, alpha-blended on top)

---

### 3. Camera

Defines observation parameters:

- View matrix (position/orientation, or derived from a transform)
- Projection (perspective, ortho, custom)
- Viewport rect (portion of the target to render into)
- Visibility mask (which layers/groups to see)
- LOD bias / distance overrides

For VR, a stereo camera holds multiple view matrices. The view uses
multi-view extensions to render both eyes in one pass.

A Camera is just data. Multiple views can share a camera.

---

### 4. Target

Where pixels end up. A pure GPU resource container (like Blender's
GPUViewport). Knows nothing about scenes or cameras.

Types:
- Window swapchain (auto-resizes with window)
- Offscreen image (render-to-texture)
- Array image (VR, cubemap faces)

Multiple views can write to the same target:
- Non-overlapping viewport rects (split screen)
- Fullscreen with alpha blend (UI overlay), ordered by view priority

The rendering system manages format negotiation and resize handling.
Swapchain targets auto-resize with the window.

---

### 5. Pipeline

Defines HOW a view turns data into pixels. Each view owns its own
pipeline instance.

The interface:

```c
typedef struct Mel_Pipeline_Type {
    str8  name;
    void  (*init)(Mel_Pipeline* self, Mel_View* view);
    void  (*draw)(Mel_Pipeline* self, Mel_Render_Manager* mgr);
    usize instance_size;
} Mel_Pipeline_Type;
```

`init` is called once per frame per view. Sets up per-frame state:
TAA jitter, post-process parameters, etc.

`draw` is called once per frame per view. Dispatches all GPU work:
culling, command generation, pass submission, post-processing.

There is NO per-object callback. The pipeline operates at the buffer
level. The Manager's storage buffers ARE the scene. The GPU reads
them directly.

Runtime registration:

```c
Mel_Pipeline_Type my_pipeline = {
    .name = S8("stylized_toon"),
    .init = toon_init,
    .draw = toon_draw,
    .instance_size = sizeof(Toon_Pipeline_Data),
};
mel_pipeline_register(&my_pipeline);
```

Games, mods, plugins can register new pipelines. The engine ships
defaults:

- `"default_2d"` - sprite batching, alpha sort, simple lighting
- `"default_3d"` - deferred, PBR, SSAO, TAA, bloom
- `"unlit"` - minimal, no lighting, no post-process
- `"overlay"` - debug visualization, gizmos, wireframes

Escape hatches:
1. Use a built-in pipeline as-is (zero effort)
2. Register a fully custom pipeline (full control)
3. (future) Compose a pipeline from stage building blocks

---

### 6. Render Manager

The persistent GPU-side object database. Bridges sources and pipelines.
Stolen from Blender's draw::Manager, enhanced with delta-sync.

The Manager is **per-source**. If two views share a source, they share
a Manager. Views diverge at the culling stage (different frustums produce
different visibility bitfields).

```
Mel_Render_Manager {
    // GPU storage buffers, indexed by Mel_Render_Handle
    StorageBuffer  transforms;     // mat4 model + model_inverse per handle
    StorageBuffer  bounds;         // AABB per handle (for GPU culling)
    StorageBuffer  infos;          // material_idx, mesh_idx, flags per handle

    // Geometry pool: ALL meshes in one set of buffers
    StorageBuffer  vertices;       // all vertices, concatenated
    StorageBuffer  meshlets;       // meshlet descriptors
    StorageBuffer  meshlet_data;   // meshlet triangle indices

    // Material pool: ALL material params in one buffer
    StorageBuffer  materials;      // all material params, indexed by material_idx

    // Visibility: per-view, per-object bitfield
    // (computed by GPU culling, consumed by command generation)

    // Handle allocator: slot allocation + free list
    // Double-buffered: previous frame's data available for temporal effects
}
```

**One handle = one draw call (sub-mesh).** A character model with 3
materials (head, body, legs) produces 3 handles in the Manager. The
source is responsible for splitting multi-material objects into
sub-meshes during sync. This keeps GPU culling and the visibility
bitfield simple: one bit per handle, one draw per visible handle.

The Manager is NOT rebuilt every frame. Sources delta-sync into it:

```c
Mel_Render_Handle h = mel_mgr_alloc(mgr);         // new object
mel_mgr_set_transform(mgr, h, transform);          // set/update transform
mel_mgr_set_bounds(mgr, h, aabb);                  // set/update bounds
mel_mgr_set_info(mgr, h, info);                    // set/update material/mesh/flags
mel_mgr_free(mgr, h);                              // remove object
```

Modified slots are tracked in a dirty bitfield. At the end of sync,
the Manager uploads ONLY the dirty ranges to the GPU. Minimal bandwidth.

**Storage agnosticism:** The Manager is a database abstraction, not
literally "GPU storage buffers." Where the data physically lives
depends on what the pipeline needs:

- GPU pipeline (Tier 1-3): data in GPU storage buffers. Dirty ranges
  uploaded via staging buffer.
- CPU pipeline (Tier 4 / software renderer): data in plain RAM arrays.
  No upload step. Pipeline reads directly.
- Hybrid (GPU culling, CPU submission): CPU_TO_GPU mapped memory.

The pipeline declares what it needs at init:

```c
void software_pipeline_init(Mel_Pipeline* self, Mel_View* view) {
    mel_mgr_request_cpu_access(view->manager, MEL_MGR_TRANSFORMS | MEL_MGR_INFOS);
}
```

If any pipeline requests CPU access, the Manager uses accessible memory.
If all pipelines are GPU-only, the Manager uses GPU-local memory.

**Resource sharing:** Shadow maps, environment probes, and other
source-level resources live alongside (or within) the Manager. If two
views share a source, they share these resources. Shadow maps are
computed once, used by both views.

---

### 7. Passes

View-agnostic GPU work containers. The pipeline owns passes internally.
Passes reference data in the Manager's storage buffers.

Pass types:

**Mel_Pass_Batched** - GPU indirect draws. Full GPU-side batching,
visibility-driven. The fast path. (Blender's PassMain / DrawMultiBuf)

**Mel_Pass_Direct** - CPU-side command recording. Deterministic order.
For small fixed draw counts (fullscreen post-process quads, UI).
(Blender's PassSimple)

**Mel_Pass_Sorted** - Like Batched but sub-passes sorted by a float key
before submission. For transparency. (Blender's PassSortable)

Passes are submitted against a specific view during `draw`. The same
pass can be submitted against multiple views (VR stereo, cubemap faces).

---

### 8. Material Base

A Material Base defines a "kind of visual" - the link between object
data in the Manager and GPU shaders.

The engine ships defaults:
- Sprite (2D quad, atlas UV, tint)
- PBR (albedo, normal, metallic, roughness, emissive)
- Unlit (color/texture, no lighting)

Custom material bases are registered at runtime:

```c
Mel_Material_Base_Desc desc = {
    .name = S8("glitch"),
    .param_size = sizeof(Glitch_Params),
    .shader = glitch_shader,
    .compat = MEL_COMPAT_FORWARD | MEL_COMPAT_DEFERRED,
};
mel_material_base_register(&desc);
```

Material parameters live in the Manager's `materials` storage buffer,
indexed by `material_idx` in the object's `infos`. Shaders read them
directly from the buffer.

---

### 9. Intermediate Render Targets

Intermediate targets (G-buffer, shadow maps, post-process buffers) are
owned by the **pipeline instance**. Each pipeline instance creates its
own intermediate textures at `init` time, sized to the view's target
resolution. This follows Blender's pattern: EEVEE's `Instance` struct
owns all intermediate textures as direct value members.

This means:
- Two views with separate pipeline instances have separate G-buffers
  (correct: different resolutions, different TAA history)
- Shadow maps are an exception: they're source-level (shared via the
  Manager), not pipeline-level, because shadows are view-independent.
  A shadow pipeline computes them once into the Manager's resources,
  and all view pipelines read from there.
- Post-process state (TAA history, motion vectors) is per-pipeline-instance
  because it's view-dependent
- HiZ pyramid (for occlusion culling) is per-pipeline-instance. The
  two-phase HiZ dance (cull against last frame's HiZ, draw visible,
  build this frame's HiZ, cull maybes, draw remainder) happens entirely
  inside the pipeline's `draw` callback. The pipeline maintains its
  own HiZ texture across frames via the double-buffered instance state.

**Transient scratch pool:** Intermediate targets that only live
during a single `draw` call (G-buffer textures, temporary resolve
targets) should be allocated from a global scratch pool, not
individually per pipeline instance. Views that don't overlap in
execution can alias the same physical memory. This prevents VRAM
explosion when many views exist (e.g., 10 editor panels).

The scratch pool uses Vulkan memory aliasing: multiple `VkImage`s
backed by the same `VkDeviceMemory` at overlapping offsets, valid
as long as their usage doesn't overlap in time. The pipeline requests
a transient target at the start of `draw` and releases it at the end.

Persistent state (TAA history, HiZ pyramid, motion vectors) is NOT
from the scratch pool — it's owned by the pipeline instance and
survives across frames.

---

## The Frame

What happens every frame:

```
1. DETERMINE ACTIVE VIEWS
   Walk all views. Skip views whose target is not due for a frame
   (independent cadences). Group views by source.

2. SYNC (per source)
   For each active source:
     source->sync(mgr)
     // Deltas applied to Manager. Dirty slots tracked.
   Manager uploads dirty ranges to GPU.

3. DRAW (per view)
   For each active view:
     a. pipeline->init(view)
     b. pipeline->draw(mgr)
        - GPU culling (compute dispatch, reads bounds + frustum)
        - Classification (opaque/transparent/shadow, GPU-side)
        - Command generation (from visibility bitfield)
        - Pass submission (indirect draws or mesh shader dispatches)
        - Post-processing
     c. Output lands in view's target

4. PRESENT (per window)
   For each window with at least one view that drew:
     Composite all views targeting this window (by priority order)
     Swap
```

**Independent cadences:**
Step 1 decides which views are "due" based on their target's refresh
rate. A 144Hz window's views run every ~7ms. A 60Hz window's views
run every ~16ms. Views targeting the slower window skip frames where
they're not due. Sync for a source only runs if at least one view
using it is active this frame. Main loop runs at fastest cadence.

---

## GPU-Driven Rendering (The Default 3D Pipeline)

The "default_3d" pipeline uses the full GPU-driven path. The user
never sees this - it's internal to the pipeline.

### Bindless Textures

All textures live in a single global descriptor array. Materials store
indices, not descriptors. One descriptor set bound per frame, never
rebound between draws.

```c
// In shader:
layout(set = 0, binding = 0) uniform sampler2D textures[];
vec4 albedo = texture(textures[material.albedo_idx], uv);
```

### The Draw Path

```
GPU Cull (compute)
  Input:  bounds[], view frustum
  Output: visible_objects bitfield

Meshlet Cull (compute) [Tier 1 only]
  Input:  visible_objects, meshlets[], frustum
  Output: visible_meshlets + indirect dispatch args
  (Backface cull, frustum cull, occlusion cull per meshlet)

Command Generation (compute)
  Input:  visible_meshlets (or visible_objects for Tier 2)
  Output: indirect draw/dispatch commands

G-Buffer Fill (mesh shader or vertex shader)
  Reads: transforms[], infos[], materials[], vertices[], textures[]
  All from storage buffers. No vertex input state. No descriptor switches.
  One indirect dispatch/draw call.

Lighting (fullscreen compute or fragment)
  Reads G-buffer + shadow maps. Writes lit result.

Forward Transparent (sorted)
  For translucent objects.

Post-Process Chain
  SSAO -> TAA -> Bloom -> Tonemap
```

Zero CPU-side per-object work during draw. Zero descriptor switches.
The CPU submits compute dispatches and indirect draws. The GPU does
everything else.

---

## Hardware Tiers

The architecture gracefully degrades across hardware capabilities.
The Manager + delta-sync model is universal. Only the pipeline's
draw path changes per tier.

### Tier 1: Mesh Shaders + Bindless

Platforms: Vulkan (NVIDIA Turing+, AMD RDNA2+), DirectX 12
Also: Metal Object Shaders (A17/M3+) when available

Full pipeline: mesh shader indirect dispatch + bindless textures +
two-level GPU culling (object + meshlet). One dispatch, one draw.

### Tier 2: Indirect Draws + Bindless

Platforms: Vulkan (all desktop), Metal (A11+), older desktop GPUs

No mesh shaders. Vertex shader pulls from storage buffers via
gl_InstanceIndex. Multi-draw indirect (vkCmdDrawIndexedIndirect /
drawIndexedPrimitives with indirect buffer). Bindless via descriptor
indexing (Vulkan) or argument buffers (Metal). Compute culling.

This is NOT a "sad path." This is what Unreal 5, Frostbite, and
most modern engines actually ship. The delta from Tier 1 is real
but not dramatic.

### Tier 3: Indirect Draws, No Bindless

Platforms: WebGPU, low-end mobile

Same as Tier 2 but without bindless textures. Need to batch by
material/texture group. Compute culling output includes material
classification to enable GPU-side sorting. More draw calls (one per
material group) but still GPU-driven culling and indirect submission.

### Tier 4: Traditional

Platforms: absolute minimum, or when object count is trivially small

Traditional draws, CPU culling, CPU batching. For snake, simple UI,
or platforms where compute isn't viable. The Manager still exists
(delta-sync still saves bandwidth), but culling and command generation
are CPU-side.

### Tier Detection

The pipeline detects hardware capabilities at `init` and selects the
appropriate code path. The source's sync code is IDENTICAL across all
tiers. The Manager's storage buffers are IDENTICAL. Only the pipeline's
`draw` implementation differs.

Hardware tessellation is also supported where available (Vulkan, Metal,
DirectX 12). It's additional shader stages in the traditional pipeline,
reading displacement data from storage buffers. The Manager doesn't
care - it's just more data in the buffers.

---

## GPU Abstraction Boundary

The rendering architecture sits on top of the `gpu.*` module layer.
Everything above `gpu.*` speaks Melody-native types. Everything inside
`gpu.*` is backend-specific.

### Two Layers of Extensibility

**GPU backend (compile-time):** Selected at build time via
`MEL_BACKEND_VULKAN`, `MEL_BACKEND_METAL`, `MEL_BACKEND_WEBGPU`.
Same public function signatures, different `.c` files linked in.
Zero runtime overhead — no vtable, no indirection.

The backend is infrastructure, not a user-facing extension point.
On any given platform, the backend is known: Vulkan on Windows/Linux,
Metal on macOS/iOS, WebGPU on web. There is no runtime switching.

**Rendering architecture (runtime):** Pipeline types, Source types,
and Material Bases are registered at runtime via function pointer
vtables. This is where plugins, mods, and game-specific code extend
the engine. These call the `gpu.*` public API with Melody-native
types — they never touch raw Vulkan/Metal/WebGPU types.

### What the Abstraction Wraps

The `gpu.*` public headers must use Melody-native types only:

- `Mel_Gpu_Format` instead of `VkFormat` / `MTLPixelFormat`
- `Mel_Gpu_Usage` flags instead of `VkBufferUsageFlags` / `MTLResourceOptions`
- `Mel_Gpu_Stage` instead of `VkPipelineStageFlags2`
- `Mel_Gpu_Load_Op` / `Mel_Gpu_Store_Op` instead of `VkAttachmentLoadOp`
- `Mel_Gpu_Index_Type` instead of `VkIndexType`
- `Mel_Gpu_Cull_Mode`, `Mel_Gpu_Blend_Mode`, `Mel_Gpu_Topology` (already
  partially abstracted via `#define` constants, need to become proper types)

Backend-specific types (`VkBuffer`, `VkImage`, `VmaAllocation`, etc.)
move into backend-internal headers (`gpu.buffer.vulkan.h`) that are
only included by `gpu.*.c` implementation files.

### What the Abstraction Does NOT Wrap

The sync/barrier model changes per backend. Instead of exposing
raw barriers (Vulkan's explicit model), the abstraction tracks
**usage intent**:

- `MEL_IMAGE_USAGE_COLOR_TARGET`
- `MEL_IMAGE_USAGE_DEPTH_TARGET`
- `MEL_IMAGE_USAGE_SHADER_READ`
- `MEL_IMAGE_USAGE_STORAGE_WRITE`
- `MEL_IMAGE_USAGE_TRANSFER_SRC` / `_DST`
- `MEL_IMAGE_USAGE_PRESENT`

The backend translates intent transitions to its native sync
mechanism. Vulkan emits pipeline barriers with stage+access masks.
Metal inserts resource hazard tracking (mostly automatic). WebGPU
handles it implicitly. The render graph already operates on
read/write intent — this aligns the gpu.* layer with that model.

### Struct Layout

Public structs become opaque or use a backend pointer:

```c
typedef struct Mel_Gpu_Buffer {
    u64    size;
    void*  mapped;
    u32    usage;
    void*  _backend;   // Vulkan: points to VkBuffer + VmaAllocation + state
} Mel_Gpu_Buffer;
```

Or fully opaque with accessors:

```c
typedef struct Mel_Gpu_Buffer Mel_Gpu_Buffer;

u64   mel_gpu_buffer_size(Mel_Gpu_Buffer* buf);
void* mel_gpu_buffer_mapped(Mel_Gpu_Buffer* buf);
```

Decision: TBD during implementation. The hybrid approach (common
fields inline, backend pointer for the rest) avoids accessor overhead
for hot-path reads while keeping backend details hidden.

### File Structure

```
gpu.buffer.h              <- public API, Melody types only
gpu.buffer.c              <- dispatches to backend (or #ifdef's)
gpu.buffer.vulkan.h       <- Vulkan-specific struct, internal only
gpu.buffer.vulkan.c       <- Vulkan implementation
gpu.buffer.metal.h        <- Metal-specific struct, internal only
gpu.buffer.metal.m        <- Metal implementation
```

The build system links only one backend's `.c`/`.m` files per platform.
No runtime dispatch overhead. A plugin compiled against `gpu.buffer.h`
works on any backend without recompilation (as long as it only uses
the public Melody-typed API).

---

## Examples

### Simple 2D Game (The "150 Lines of Snake" Path)

```c
Mel_World* world = mel_world_create();

ecs_entity_t snake = ecs_new(world);
ecs_set(world, snake, Mel_Transform2D, { .pos = {5, 5} });
ecs_set(world, snake, Mel_Sprite, { .color = GREEN });

Mel_View* view = mel_view_create(&(Mel_View_Desc){
    .source = mel_source_ecs(world),
    .camera = mel_camera_ortho(0, 320, 0, 240),
    .target = mel_target_from_window(window),
});
// pipeline defaults to "default_2d"
// The user just updates entity components in their sim tick.
```

### Split Screen Co-op

```c
Mel_Render_Source* src = mel_source_ecs(game_world);

Mel_View* p1_view = mel_view_create(&(Mel_View_Desc){
    .source = src,
    .camera = mel_camera_follow(player1, &(Mel_Camera_Desc){
        .projection = mel_projection_ortho(0, 320, 0, 240),
        .viewport = { 0, 0, 0.5f, 1.0f },
    }),
    .target = window_target,
});

Mel_View* p2_view = mel_view_create(&(Mel_View_Desc){
    .source = src,
    .camera = mel_camera_follow(player2, &(Mel_Camera_Desc){
        .projection = mel_projection_ortho(0, 320, 0, 240),
        .viewport = { 0.5f, 0, 0.5f, 1.0f },
    }),
    .target = window_target,
});

// Sync runs ONCE (same source, same Manager).
// GPU culling runs TWICE (different frustums).
// Draw runs TWICE (different viewports on same target).
```

### AAA 3D With Post-Processing

```c
Mel_View* view = mel_view_create(&(Mel_View_Desc){
    .source   = mel_source_ecs(game_world),
    .camera   = main_cam,
    .target   = window_target,
    .pipeline = S8("default_3d"),
});

// The "default_3d" pipeline internally does:
//   G-buffer fill (mesh shaders or indirect draws)
//   Shadow pass
//   Lighting (clustered or tiled)
//   Forward transparent
//   SSAO -> TAA -> Bloom -> Tonemap
// The user doesn't see any of this.
```

### Mixed Sources (ECS + Particles + Debug)

```c
Mel_Render_Source* scene     = mel_source_ecs(game_world);
Mel_Render_Source* particles = mel_source_manual_create();
Mel_Render_Source* debug     = mel_source_manual_create();

Mel_Render_Source* everything = mel_source_composite_create();
mel_source_composite_add(everything, scene);
mel_source_composite_add(everything, particles);
mel_source_composite_add(everything, debug);

Mel_View* view = mel_view_create(&(Mel_View_Desc){
    .source   = everything,
    .camera   = main_cam,
    .target   = window_target,
    .pipeline = S8("default_3d"),
});

// Pipeline draws all objects regardless of source origin.
// Routes to passes based on material type.
```

### Multi-Window Independent Cadences

```c
Mel_Render_Source* src = mel_source_ecs(game_world);

// 144Hz gaming monitor
Mel_View* game_view = mel_view_create(&(Mel_View_Desc){
    .source = src,
    .camera = game_cam,
    .target = mel_target_from_window(main_window),  // 144Hz
    .pipeline = S8("default_3d"),
});

// 60Hz debug monitor showing wireframe
Mel_View* debug_view = mel_view_create(&(Mel_View_Desc){
    .source = src,
    .camera = debug_cam,
    .target = mel_target_from_window(debug_window),  // 60Hz
    .pipeline = S8("overlay"),
});

// Main loop runs at 144Hz.
// game_view draws every frame.
// debug_view draws every ~2.4 frames (when due).
// Source sync runs every frame (game_view is always active).
```

---

## Interfaces

These are the vtable-style extension points. They use function pointers
because they are the seams where runtime extensibility lives
(MEL-ENGINE-IV). They are few (3), tiny (1-2 functions each), and
called rarely (once per frame, not per object).

### Mel_Render_Source_Type

```c
typedef struct Mel_Render_Source_Type {
    str8  name;
    void  (*sync)(Mel_Render_Source* self, Mel_Render_Manager* mgr);
    usize instance_size;
} Mel_Render_Source_Type;
```

One function. Delta-syncs the source's data into the Manager.
Called once per frame per active source.

### Mel_Pipeline_Type

```c
typedef struct Mel_Pipeline_Type {
    str8  name;
    void  (*init)(Mel_Pipeline* self, Mel_View* view);
    void  (*draw)(Mel_Pipeline* self, Mel_Render_Manager* mgr);
    usize instance_size;
} Mel_Pipeline_Type;
```

Two functions. `init` sets per-frame state. `draw` dispatches all GPU work.
Called once per frame per active view.

### Mel_Material_Base_Desc

```c
typedef struct Mel_Material_Base_Desc {
    str8           name;
    u32            param_size;
    Mel_Gpu_Shader shader;
    u32            compat;       // MEL_COMPAT_FORWARD | MEL_COMPAT_DEFERRED | ...
} Mel_Material_Base_Desc;
```

Not a vtable — a registration descriptor. Tells the pipeline how to
handle objects with this material type. Registered once at startup
(or at runtime by mods/plugins).

---

## Types

Concrete types that make up the system. These are NOT extensible
via vtables — they are the engine's internal machinery.

### Mel_View

```c
typedef struct Mel_View_Desc {
    Mel_Render_Source* source;
    Mel_Camera         camera;
    Mel_Target*        target;
    str8               pipeline;    // name of registered pipeline type
    i32                priority;    // compositing order on shared targets
} Mel_View_Desc;
```

Created via `mel_view_create(Mel_View_Desc*)`. Owns its pipeline
instance (allocated at `instance_size` from the pipeline type).

### Mel_Camera

```c
typedef struct Mel_Camera {
    Mel_Mat4   view;
    Mel_Mat4   projection;
    Mel_Vec4   viewport;       // x, y, w, h as fraction of target [0..1]
    u32        visibility_mask;
    f32        lod_bias;
    u32        view_count;     // 1 = mono, 2 = stereo (VR)
    Mel_Mat4   stereo_views[2];
} Mel_Camera;
```

Pure data. Helper constructors: `mel_camera_ortho()`,
`mel_camera_perspective()`, `mel_camera_follow()`, `mel_camera_stereo()`.

### Mel_Target

```c
typedef struct Mel_Target Mel_Target;
```

Opaque. Created via:
- `mel_target_from_window(Mel_Window*)` — swapchain, auto-resizes
- `mel_target_offscreen(Mel_Target_Desc*)` — render-to-texture
- `mel_target_array(Mel_Target_Desc*)` — VR / cubemap

Internally owns output textures + framebuffers. Knows nothing about
scenes, cameras, or pipelines. (Blender's GPUViewport equivalent.)

### Mel_Render_Manager

```c
typedef struct Mel_Render_Manager Mel_Render_Manager;
```

Opaque. Per-source. Created automatically when a source is first used.

Public API for sources:

```c
Mel_Render_Handle mel_mgr_alloc(Mel_Render_Manager* mgr);
void mel_mgr_free(Mel_Render_Manager* mgr, Mel_Render_Handle h);

void mel_mgr_set_transform(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Mat4 transform);
void mel_mgr_set_bounds(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Render_Bounds bounds);
void mel_mgr_set_info(Mel_Render_Manager* mgr, Mel_Render_Handle h, Mel_Render_Info info);

void mel_mgr_request_cpu_access(Mel_Render_Manager* mgr, u32 buffer_mask);
```

Public API for pipelines:

```c
void mel_mgr_cull(Mel_Render_Manager* mgr, Mel_View* view);
void mel_mgr_upload_dirty(Mel_Render_Manager* mgr);
```

### Mel_Render_Handle

```c
typedef struct Mel_Render_Handle { u32 idx; u32 gen; } Mel_Render_Handle;
```

Generational handle into the Manager's storage arrays. Sources store
these to track the mapping between their objects and the Manager's slots.

### Mel_Render_Info

```c
typedef struct Mel_Render_Info {
    u32 material_base_id;
    u32 material_idx;      // index into materials storage buffer
    u32 mesh_idx;          // index into geometry pool
    u32 flags;             // MEL_RF_CAST_SHADOW, MEL_RF_STATIC, etc.
    u32 layer_mask;        // visibility layers
} Mel_Render_Info;
```

Per-object metadata uploaded to the Manager. The GPU reads this to
route objects to passes and look up materials.

### Mel_Render_Bounds

```c
typedef struct Mel_Render_Bounds {
    Mel_Vec3 center;
    Mel_Vec3 extents;
} Mel_Render_Bounds;
```

World-space AABB. Used by GPU culling.

### Pass Types

```c
typedef struct Mel_Pass_Batched Mel_Pass_Batched;  // GPU indirect, visibility-driven
typedef struct Mel_Pass_Direct  Mel_Pass_Direct;   // CPU-side, deterministic order
typedef struct Mel_Pass_Sorted  Mel_Pass_Sorted;   // GPU indirect, sorted by key
```

Owned by pipeline instances as value members. Not extensible.

### Mel_Render_Source

```c
typedef struct Mel_Render_Source {
    Mel_Render_Source_Type* type;
    void*                  instance;  // allocated at type->instance_size
    Mel_Render_Manager*    manager;   // assigned when first used by a view
} Mel_Render_Source;
```

### Mel_Pipeline

```c
typedef struct Mel_Pipeline {
    Mel_Pipeline_Type* type;
    void*              instance;  // allocated at type->instance_size
    Mel_View*          view;      // the view this instance belongs to
} Mel_Pipeline;
```

---

## Module Lifecycle: Boot System Integration

The rendering architecture builds on the engine's event-driven boot
system (see `design/engine.boot.md`).

### How Rendering Modules Hook Into Boot

Rendering subsystems use constructors for two things:
1. Initialize their own event channels
2. Subscribe to dependency events (`mel_gpu_device_ready`, etc.)

No GPU work happens in constructors. GPU-dependent init is triggered
by events fired from the boot job.

### Event Channels for Rendering

```c
extern Mel_Event_Channel mel_render_source_ready;
extern Mel_Event_Channel mel_render_view_ready;
extern Mel_Event_Channel mel_render_material_ready;
```

### Constructor Priority Map (Rendering Modules)

```
 100  job system               — worker threads
 101  logging                  — log system
 150  gpu.device               — init channel only (no GPU work)
 151  gpu.shader (slang)       — init channel only
 200  sprite pass              — init channel + subscribe to gpu_ready
 201  text pass                — init channel + subscribe to gpu_ready
 202  mesh pass                — init channel + subscribe to gpu_ready
 250  async.aio                — dispatch_io / io_uring
 280  vfs                      — mount table
 290  font.desc                — descriptor pool + dispatch CPU font load jobs
 300  texture pool             — init channel + subscribe to 3 pass_ready
 301  font.atlas/sdf/msdf pool — init channel + subscribe to texture_pool_ready
 500  window                   — window management
 501  swapchain                — swapchain pool
 510  render.source            — source registry + init channel
 511  render.view              — view registry
 512  render.material          — Material Base registry
 520  render.pipeline          — Pipeline type registry + built-in pipeline registration
```

### Boot Cascade for Rendering

```
mel_boot_job (fiber)
    |
GPU device + Slang init
    |
fire(mel_gpu_device_ready)
 /       |       \
sprite  text   mesh pass     << parallel shader compilation >>
pass    pass
 |       |       |
fire(own _ready events)
 \       |       /
texture_pool (atomic countdown, last pass triggers init)
    |
fire(mel_texture_pool_ready)
    |
font technique pools init (subscribe to texture_pool_ready)
    |
fire(mel_font_pool_ready)
    |
rendering registries init their GPU resources
    |
---- all inside job functions, before counter decrement ----
    |
shader_counter → 0
    |
boot job wakes → SDL_SignalSemaphore → main thread resumes
    |
app_init()
```

CPU-heavy startup work (font parsing, file I/O) dispatches as jobs
from constructors and runs in parallel with the entire boot cascade.
By the time the boot job signals completion, both CPU and GPU init
are done.

### Two-Phase Startup: CPU Then GPU

Constructors dispatch **CPU work** — the GPU device doesn't exist yet.
GPU work is triggered by `mel_gpu_device_ready` events, which fire
from the boot job after the device is created.

Modules that need both CPU and GPU init subscribe to the appropriate
event and dispatch their GPU work from the listener:

```c
__attribute__((constructor(301)))
static void mel__font_sdf_register(void) {
    mel_event_channel_init(&mel_font_sdf_ready, mel_alloc_heap());
    mel_event_channel_on(&mel_texture_pool_ready, mel__font_sdf_on_pool_ready, nullptr);
}

static void mel__font_sdf_on_pool_ready(void* ctx, const void* event) {
    mel_font_sdf_pool_init_gpu(&s_pool, ...);
    mel_event_channel_fire(&mel_font_sdf_ready, nullptr);
}
```

### Module-Static Pool Pattern

Modules that manage a pool of resources use a static pool inside
the .c file, initialized by constructor:

```c
static Mel_Font_SDF_Pool s_pool;

__attribute__((constructor(301)))
static void mel__font_sdf_register(void) {
    s_pool = (Mel_Font_SDF_Pool){0};
    mel_slotmap_init(&s_pool.slotmap, ...);
    mel_hashmap_init(&s_pool.path_to_handle, ...);
    mel_event_channel_init(&mel_font_sdf_ready, mel_alloc_heap());
    mel_event_channel_on(&mel_texture_pool_ready, mel__font_sdf_on_pool_ready, nullptr);
}
```

The public API hides the pool entirely. Callers don't see it:

```c
Mel_Font_SDF_Handle mel_font_sdf_create(Mel_Font_Desc_Handle desc, ...);
```

Internally, `_create` uses the static `s_pool`. The pool is exposed
for internal/extension use via a double-underscore accessor (MEL-X-003):

```c
Mel_Font_SDF_Pool* mel__font_sdf_pool(void);
```

### Shutdown

Reverse cascade via `mel_shutdown_begin` event (see `design/engine.boot.md`).
Each rendering module subscribes in its constructor and cleans up GPU
resources in the listener. Destructors handle data structure cleanup.

---

## Prerequisites

What must exist before implementing this architecture.

### Already Exists

- **GPU buffer management** (`gpu.buffer.*`) — create, map, upload
- **GPU command buffers** (`gpu.cmd.*`) — recording, includes `mel_gpu_cmd_draw_indexed_indirect`
- **GPU compute pipelines** (`gpu.pipeline.*`) — `MEL_GPU_PIPELINE_COMPUTE` supported
- **GPU descriptor sets** (`gpu.descriptor.*`) — `MEL_GPU_DESCRIPTOR_STORAGE_BUFFER` supported
- **GPU shader compilation** (`gpu.shader.*`) — slang integration
- **GPU swapchain** (`gpu.swapchain.*`) — window surface management
- **GPU device capabilities** (`gpu.device.*`) — `mesh_shader`, `multi_draw_indirect`, `shader_draw_parameters`, `buffer_device_address` already tracked
- **Slotmap** (`collection.slotmap.*`) — generational handles with free list
- **Bitset** (`collection.bitset.*`) — for dirty tracking
- **ECS** (`ecs.world.*`) — entity management, queries, observers
- **Camera** (`render.camera.*`) — basic camera exists (will be reworked)
- **Existing render files** (`render.*`) — naming groundwork exists, code will be replaced

### Missing (Must Add Before or During)

- **Descriptor indexing / bindless** — not in `Mel_Gpu_Capabilities`.
  Need to add `descriptor_indexing` capability flag and enable
  `VK_EXT_descriptor_indexing` (or Vulkan 1.2 core) at device creation.
  Required for Tier 1-2 bindless textures.

- **Mesh shader command** — `gpu.cmd` has indirect draws but no
  `vkCmdDrawMeshTasksIndirectEXT`. Need to add for Tier 1.

- **Timeline semaphores** — not checked, but needed for multi-queue
  and potentially for multi-window independent cadence sync.

---

## Foreplay

Work that can be done independently BEFORE touching the rendering
architecture. Each item is self-contained and valuable on its own.

### 1. Global Texture Table — DONE

A bindless descriptor set manager. Allocates slots in a large
descriptor array, returns indices. Textures are added/removed at
runtime. One descriptor set bound per frame.

```
render.texture_table.h / .c
```

Requires: descriptor indexing capability in gpu.device.
Self-contained: no dependency on the rest of the architecture.
Tests: create table, add textures, verify indices, remove, reuse slots.

### 2. Descriptor Indexing Support — DONE

Add `descriptor_indexing` to `Mel_Gpu_Capabilities`. Enable
`VK_EXT_descriptor_indexing` (or Vulkan 1.2 descriptorIndexing feature)
at device creation. Add `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT`
and `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT` support
to descriptor set creation.

```
gpu.device.c (capability detection)
gpu.descriptor.c (binding flags)
```

Self-contained. Prerequisite for the texture table.

### 3. Mesh Shader Command Support — DONE

Add `mel_gpu_cmd_draw_mesh_tasks_indirect` to `gpu.cmd`. Load
`vkCmdDrawMeshTasksIndirectEXT` via volk. Gate behind
`capabilities.mesh_shader`.

```
gpu.cmd.h / .c
```

Self-contained. Small addition to existing module.

### 4. Storage Buffer Handle-Indexed Wrapper — DONE

A typed wrapper around `Mel_Gpu_Buffer` that provides:
- Handle-based alloc/free (backed by slotmap)
- Dirty tracking (backed by bitset)
- Bulk upload of only dirty ranges
- Optional CPU mirror for software pipelines

This is the Manager's internal storage primitive. Building it
standalone makes the Manager implementation trivial.

```
gpu.storage_pool.h / .fwd.h / .c
```

Requires: gpu.buffer, collection.slotmap, collection.bitset.
Tests: alloc handles, set data, verify dirty tracking, upload dirty,
free and reuse.

### 5. Compute Culling Shader — DONE

A compute shader that reads an array of AABBs + a frustum (6 planes)
and writes a visibility bitfield. One bit per object.

```
shaders/cull_objects.slang
render.cull.h
```

Self-contained. Can be tested independently with a test compute
pipeline: upload N bounds + frustum, dispatch, read back bitfield,
verify against CPU reference implementation.

### 6. ECS Change Detection Helpers — DONE

Wrappers around flecs change detection to efficiently produce
delta lists (added, removed, modified entities) for the ECS source's
`sync` function.

Need to verify flecs supports:
- `ecs_query_changed()` — does the query have new results?
- Observers for `OnAdd` / `OnRemove` events
- Per-component dirty flags or generation counters

If flecs doesn't give us fine-grained "which entities changed which
components," we may need to maintain our own generation counters
per entity-component pair.

```
render.ecs.delta.h / .c
```

Requires: ecs.world.
Tests: add entities, modify, remove, verify delta lists are correct.

### 7. Per-Frame Staging System — DONE

A ring buffer or per-frame arena for CPU->GPU uploads. Instead of
mapping/unmapping buffers per dirty range, batch all dirty writes into
a staging buffer, then issue one copy command.

Could be as simple as a per-frame arena that resets each frame,
with a `mel_staging_write(staging, dst_buffer, offset, data, size)`
that queues a copy.

```
gpu.staging.h / .c
```

Requires: gpu.buffer, gpu.cmd, allocator.arena.
Self-contained.

### 8. Indirect Draw Buffer Helpers — DONE

Typed buffer for `VkDrawIndexedIndirectCommand` or
`VkDrawMeshTasksIndirectCommandEXT`. Provides:
- Append commands (CPU side for Tier 4)
- GPU-writable for compute command generation (Tier 1-3)
- Draw count tracking

```
gpu.indirect.h / .c
```

Requires: gpu.buffer.
Self-contained.

### 9. Transient Scratch Pool — DONE

A GPU memory pool for intermediate render targets that only live
during a single pipeline `draw` call (G-buffer textures, temporary
resolve targets, blur intermediates). Uses Vulkan memory aliasing:
multiple VkImages backed by the same VkDeviceMemory at overlapping
offsets, valid as long as their usage doesn't overlap in time.

```
gpu.scratch_pool.h / .c
```

API:
```c
Mel_Gpu_Image mel_scratch_acquire(Mel_Scratch_Pool* pool, Mel_Scratch_Desc desc);
void mel_scratch_release(Mel_Scratch_Pool* pool, Mel_Gpu_Image img);
```

Acquire at the start of `draw`, release at the end. Views that
execute sequentially reuse the same physical memory. Prevents VRAM
explosion with many views (e.g., 10 editor panels each needing a
G-buffer).

Requires: gpu.image, allocator (for memory aliasing bookkeeping).
Self-contained. Tests: acquire, release, verify aliasing, verify
different-sized requests.

### 10. GPU Type Abstraction Pass — IN PROGRESS

Replace all Vulkan types in `gpu.*` public headers with Melody-native
equivalents. This is the foundation for multi-backend support.

Phase 1: Define Melody-native type constants (`Mel_Gpu_Format`,
`Mel_Gpu_Usage`, `Mel_Gpu_Stage`, `Mel_Gpu_Load_Op`, etc.)

Phase 2: Replace all `VkFormat`, `VkBufferUsageFlags`,
`VkImageUsageFlags`, `VkShaderStageFlags`, `VkIndexType`, etc. in
public headers with Melody types.

Phase 3: Move `VkBuffer`, `VkImage`, `VmaAllocation`, etc. behind
backend-internal headers. Public structs get `_backend` pointers
or become opaque.

Phase 4: Add conversion functions in each backend
(`mel__gpu_format_to_vk()`, etc.)

```
gpu.types.h               <- Mel_Gpu_Format, Mel_Gpu_Usage, etc.
gpu.buffer.vulkan.h       <- VkBuffer, VmaAllocation (internal)
gpu.image.vulkan.h        <- VkImage, VkImageView (internal)
```

Requires: nothing. Purely mechanical refactor of existing code.
Self-contained. Can be done file-by-file without breaking anything
(each file converted independently, old Vk types replaced with
Mel types + conversion at the backend boundary).

Currently 22 header files include `<vulkan/vulkan.h>`. Goal: only
`gpu.*.vulkan.*` files include Vulkan headers.

### Suggested Order

The foreplay items have these dependencies:

```
2. Descriptor Indexing ──> 1. Texture Table
                      (no deps) 3. Mesh Shader Commands
                      (no deps) 4. Storage Pool
                      (no deps) 5. Cull Shader
                      (no deps) 6. ECS Delta Helpers
                      (no deps) 7. Staging System
                      (no deps) 8. Indirect Draw Helpers
                      (no deps) 9. Scratch Pool
                      (no deps) 10. GPU Type Abstraction
```

Items 3-10 are fully independent and can be done in any order (or
in parallel). Item 2 must come before item 1. All of these can be
done before touching any render.* code.

Item 10 is unique: it can be done incrementally alongside other work.
Each gpu.* file can be converted independently. It does NOT block
any other foreplay item — the Vulkan backend still works, just with
Melody types at the API boundary and conversion functions internally.

Once all foreplay is done, the main implementation is:
1. Mel_Render_Manager (uses items 4, 7, 8)
2. Mel_Render_Source + ECS source (uses item 6)
3. Mel_Pipeline + default_2d pipeline (uses items 1, 5, 9)
4. Mel_View + Mel_Target + frame loop
5. default_3d pipeline (uses items 1, 3, 5, 9)
6. Multi-window / independent cadence support
