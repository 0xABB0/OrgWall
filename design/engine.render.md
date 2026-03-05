# Rendering — Production & Interchange

## Philosophy

**Three independent pieces. Each replaceable. They compose, they don't depend.**

1. **ECS sync systems** (production) — default-provided, replaceable ECS systems
   that sync game components into render lists. "This sprite exists at this position."
2. **Render lists** (interchange) — typed, camera-agnostic databases of world data.
   Persistently mapped GPU buffers. They don't know about cameras, windows, or
   how many times they'll be drawn.
3. **Render graph + passes** (consumption) — a global data-flow DAG. Passes read
   lists and targets, write lists and targets. Swapchains are leaves. The graph
   derives execution order from the topology.

The system is consistent with itself but lets you override every single step.

→ Consumption side (render graph, passes, GPU-driven, bindless, materials): `engine.render.graph.md`

## Status

This document describes the target rendering architecture (vNext).

Current implementation exists in:
- `melody/render.graph.h`
- `melody/render.graph.c`
- `melody/core.engine.c`

```
ECS SYNC SYSTEMS (production — replaceable):
  │  Default systems observe Mel_Sprite, Mel_Mesh, etc.
  │  and maintain render list entries automatically.
  │  Replace with your own sync logic if you want.
  │
  ▼
RENDER LISTS (interchange — persistently mapped GPU buffers):
  ├─ Sprite list: {pos, size, uv, color, tex, depth}
  ├─ Mesh list: {transform, material_id, mesh_id, depth}
  ├─ Particle list: {pos, size, lifetime, velocity}
  └─ [your type]: {your data}
  │
  │  Also fed by: draw APIs, manual submission, compute passes
  │
  ▼
RENDER GRAPH (consumption — global DAG):
  Passes declare read/write. Graph derives order.
  ├─ LOD select (compute) — reads meshes, writes lod_resolved
  ├─ Cull (compute)       — reads lod_resolved, writes draw_cmds
  ├─ Shadow (graphics)    — reads meshes, writes shadow_map
  ├─ Opaque (graphics)    — reads sprites + draw_cmds, reads shadow_map, writes hdr
  ├─ Post-process         — reads hdr, writes swapchain
  └─ [your pass]
```

---

## Camera

Pure data type representing a view into the world. Defined in
`melody/render.camera.h`.

```c
struct Mel_Camera {
    Mel_Mat4 view;
    Mel_Mat4 projection;
    Mel_Vec3 position;
};
```

No methods beyond one inline convenience:

```c
static inline Mel_Mat4 mel_camera_vp(const Mel_Camera* cam)
{
    return mel_mat4_mul(cam->projection, cam->view);
}
```

Users build cameras using existing math functions:

```c
Mel_Camera cam = {
    .view = mel_mat4_look_at(eye, target, up),
    .projection = mel_mat4_perspective(fov, aspect, near, far),
    .position = eye,
};

Mel_Camera ortho_cam = {
    .view = mel_mat4_identity(),
    .projection = mel_mat4_ortho(0, w, 0, h, -1, 1),
    .position = mel_vec3(0, 0, 0),
};
```

Passes receive a `const Mel_Camera*` via `Mel_Render_Pass_Ctx`. The graph
stores the pointer (not a copy) — the game owns the camera and updates it
live, the graph reads at execute time.

Camera is optional in `Mel_Pass_Desc`. Passes that don't need a camera
(post-processing, fullscreen effects) leave it null.

---

## Render Lists

### Why Typed Per-Pipeline?

A sprite pipeline needs `{pos, uv, color, tex_id}`. A PBR mesh pipeline needs
`{transform, material_id, mesh_id}`. A particle pipeline needs `{pos, size, lifetime}`.
Different data. Different layouts. Different GPU buffers.

A render list's entry layout is defined by the pipeline that consumes it.
The render list is a generic container. The pipeline knows the type.

### Why Camera-Agnostic?

A sprite at position (100, 200) is at that position regardless of which camera is
looking at it. The render list stores world-space data. The pass applies the
view-projection transform. This means the same list can be drawn N times per frame
from N different cameras without touching the entry data.

### Ownership

Lists exist independently. They are not owned by the graph or by windows. The
graph references them. Any number of passes can read the same list. Compute
passes can write to lists, feeding data to downstream passes.

Lists can be owned by:
- A world (default sync lists created during `mel_world_create`)
- The game (custom lists for particles, debug draws, etc.)
- Nobody in particular (just created and passed around)

### Data Structure

→ Full struct definition: `engine.render.graph.md`

```c
Mel_Render_List* mel_render_list_create(str8 name, u32 entry_stride, const Mel_Alloc* alloc);
void mel_render_list_destroy(Mel_Render_List* list);
```

The `entries` pointer is a persistently mapped GPU pointer
(`VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | DEVICE_LOCAL_BIT`). CPU writes go
directly into GPU-visible memory. No staging, no upload, no copy.

### Insert (Retained)

```c
u32 mel_render_list_insert(Mel_Render_List* list, u64 sort_key);
```

Returns entry index. The entry persists until explicitly removed. Pops from
the free list before growing. Used by ECS sync systems at entity creation time.

Write to the returned entry:

```c
typedef struct {
    Mel_Vec2 pos;
    Mel_Vec2 size;
    Mel_Rect uv;
    u32 color;
    Mel_Texture_Handle tex;
    f32 depth;
} Mel_Sprite_Entry;

u32 idx = mel_render_list_insert(sprite_list, sort_key);
Mel_Sprite_Entry* e = mel_render_list_get(sprite_list, idx);
*e = (Mel_Sprite_Entry){ .pos = pos, .size = size, .tex = tex_handle, ... };
```

This writes directly to mapped GPU memory. The GPU sees it immediately
(after the appropriate memory barrier at graph execution time).

### Push (Ephemeral)

```c
void* mel_render_list_push(Mel_Render_List* list, u64 sort_key);
```

Returns pointer to new entry. Entry lives for one frame (frame arena backed).
Cleared automatically at frame boundary. Used by production hooks, variable
updates, and per-frame dynamic content (interpolated positions, debug draws).

### Get (Retained Lists)

```c
void* mel_render_list_get(Mel_Render_List* list, u32 entry_index);
```

Returns pointer to an existing entry by index. Used by ECS sync systems and
retained producers to update entries in-place without rebuilding the list.
Writes directly to mapped GPU memory.

```c
void mel_render_list_update_key(Mel_Render_List* list, u32 entry_index, u64 sort_key);
```

Updates the sort key for an existing entry. Marks the list dirty.

```c
void mel_render_list_remove(Mel_Render_List* list, u32 entry_index);
```

Removes an entry and pushes its index onto the free list for reuse.

### Sort

```c
mel_render_list_sort(list);
```

Radix sort on packets by `sort_key`. Only moves 12-byte packets — entry data stays
in place. Iterate after sort: `entries[packets[i].entry_index * stride]`.

### Sort Keys

Sort key layout is **pipeline-defined**. The engine provides helpers:

```c
u64 mel_sort_key_sprite(u8 layer, f32 depth, u16 material, u16 texture_bucket);
u64 mel_sort_key_opaque(f32 depth);       // front-to-back (inverted depth)
u64 mel_sort_key_transparent(f32 depth);  // back-to-front
```

Common layout: `[Layer(8) | Depth(24) | Material(16) | Texture(16)]` = 64 bits.
But any pipeline can define its own key format.
`texture_bucket` is a sort grouping key, not the full bindless `tex_id`.

---

## ECS Sync Systems (Production)

Default-provided ECS systems that observe game components and maintain render list
entries. These are regular ECS systems registered on the simulation's world — not
part of the render graph. They are independently replaceable.

Entities have zero rendering-specific components. They are pure game state
(`Mel_Sprite`, `Mel_CTransform`, etc.). The sync systems decide what to observe
and which lists to populate.

The sync systems maintain entity→entry_index mappings as internal side-tables.
`OnAdd` inserts a new entry (writes to mapped GPU memory). `OnSet` updates
in-place via the side-table (writes to mapped GPU memory). `OnRemove` frees
the entry slot.

The default sync systems are registered during `mel_world_create`. Replace
them by removing the defaults and registering your own ECS systems that write
to render lists however you want.

Sync systems write to lists. Lists exist independently. The graph routes lists
through passes to outputs. The sync system never needs to know about windows,
swapchains, or the render graph.

---

## Other Sources (Non-ECS)

### Production Hooks

Producers can be attached directly to a render list. The engine calls them
once per frame before list sorting and graph execution.

```c
typedef void (*Mel_Render_Producer_Fn)(Mel_Render_List* list, void* user);

void mel_render_list_add_producer(Mel_Render_List* list, Mel_Render_Producer_Fn fn, void* user);
void mel_render_list_remove_producer(Mel_Render_List* list, Mel_Render_Producer_Fn fn);
```

Hooks push ephemeral entries each frame. For retained lists, hooks are optional;
the explicit insert/get/remove paths maintain entries incrementally.

### Draw API

Three modes. **Not polymorphic — different functions and code paths.**

#### Ephemeral (Rebuilt Each Frame)

Frame-arena allocated. Fastest for dynamic content.

```c
void frame_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Mel_Vec2 pos = mel_vec2_lerp(prev_pos, curr_pos, alpha);
    mel_draw_sprite(sprite_list, pos, tex_handle, color);
}
```

Pushes entries with computed sort keys. Cleared automatically at frame start.
No commit, no persist.

#### Retained (Stored Draw Calls)

Two persistent geometry pipelines: shapes (non-indexed, triangle list) and indexed tris.
CPU arrays accumulate vertex data. `commit()` uploads to GPU. Buffers persist.

```c
Mel_Draw_Context* ctx = mel_draw_ctx_create(materials);

mel_draw_ctx_rect(ctx, 10, 10, 200, 50, 0, 0, style);
mel_draw_ctx_line(ctx, from, to, line_style);
mel_draw_ctx_text(ctx, S8("Hello"), 20, 20, font);
mel_draw_ctx_commit(ctx);
```

After commit, GPU buffers persist. Renderer draws them every frame without CPU work.
Only call draw + commit again when content changes.

Good for: static UI, debug overlays, editor chrome.

#### Pre-Rendered (Framebuffer Cached)

Draw into offscreen framebuffer. Result is a texture handle.

```c
Mel_Draw_Fb* fb = mel_draw_fb_create(512, 512);
mel_draw_fb_rect(fb, ...);
mel_draw_fb_text(fb, S8("Minimap"), ...);
mel_draw_fb_commit(fb);
// fb->texture is a regular texture handle. use it anywhere.
```

Re-render only on explicit invalidation. Good for: minimaps, complex panels.

### Manual Submission

Push directly to any render list. Total control.

```c
for (u32 i = 0; i < scene->count; i++) {
    Mel_Mesh_Entry* e = mel_render_list_push(meshes, sort_key);
    e->transform = scene->nodes[i].world_transform;
    e->mesh_id = scene->nodes[i].mesh;
    e->material_id = scene->nodes[i].material;
}
```

---

## Resolved Questions

1. ~~**Thread safety of render list pushes**~~: **Resolved.** Render lists are
   main-thread submission by default. Job fibers produce into thread-local staging
   buffers that are merged on main thread before graph execution.

2. ~~**Retained list updates**~~: **Resolved.** ECS sync systems maintain
   entity→entry_index side-tables internally. Updates go through
   `mel_render_list_get()`. `OnRemove` frees entry slots.

3. ~~**ECS sync → render graph wiring**~~: **Resolved.** Lists are independent
   resources. Sync systems write to lists. The graph routes lists through
   passes to outputs. The sync system doesn't need to know about windows,
   swapchains, or the render graph. `mel_world_present_to(world, window)`
   wires default passes as a convenience.
