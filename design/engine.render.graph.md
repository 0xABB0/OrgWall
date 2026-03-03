# Rendering — Render Graph

## Status

This document describes the target render graph architecture (vNext).
Supersedes the previous per-window graph design.

Current implementation exists in:
- `melody/render.graph.h`
- `melody/render.graph.c`

→ Production and interchange (render lists, ECS sync, draw API): `engine.render.md`

---

## Core Idea

The render graph is a **global data-flow DAG**. It is not per-window.

Passes are transform nodes. They declare what they read and what they write.
The graph derives execution order, inserts barriers, and manages resource
lifetimes — all from the topology.

```
RENDER LISTS (sources — camera-agnostic world data):
  ├─ Sprite list: {pos, size, uv, color, tex, depth}
  ├─ Mesh list: {transform, material_id, mesh_id, depth}
  ├─ Particle list: {pos, size, lifetime, velocity}
  └─ [your type]: {your data}
  │
  ▼
PASSES (transform nodes — read resources, write resources):
  ├─ LOD select (compute) — reads meshes, writes lod_resolved
  ├─ Cull (compute)       — reads lod_resolved, writes draw_cmds
  ├─ Shadow (graphics)    — reads meshes, writes shadow_map
  ├─ Opaque (graphics)    — reads sprites + draw_cmds, reads shadow_map, writes hdr
  ├─ Post-process         — reads hdr, writes swapchain
  └─ [your pass]
  │
  ▼
SWAPCHAINS (leaves — presentation targets):
  ├─ Game window swapchain
  ├─ Editor window swapchain
  └─ [your window]
```

Resources flow through the graph. Passes transform them. Swapchains are where
pixels end up. The graph compiles the DAG once (and recompiles on topology
changes), then executes it every frame.

---

## Resources

Two kinds of resources flow through the graph.

### Render Lists

Typed, camera-agnostic arrays of world data. Persistently mapped GPU buffers.

Lists exist independently — they are not owned by the graph or by windows.
The graph references them. Multiple passes can read the same list. Compute
passes can write to lists, feeding data to downstream passes.

```c
Mel_Render_List* mel_render_list_create(str8 name, u32 entry_stride, const Mel_Alloc* alloc);
void mel_render_list_destroy(Mel_Render_List* list);
```

**Persistently mapped GPU buffers.** The `entries` pointer in a render list
points directly to GPU-visible memory (`VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
DEVICE_LOCAL_BIT`). CPU writes (ECS sync, manual push) go directly into the
buffer. No staging. No upload. No copy. The GPU reads the same memory.

```c
struct Mel_Render_List {
    str8 name;

    u8* entries;                 // persistently mapped GPU pointer
    u32 entry_stride;

    Mel_Render_Packet* packets;  // sort key + entry index (CPU-side)
    u32 count;
    u32 capacity;

    u32* free_indices;           // recycled entry slots (retained lists)
    u32 free_count;

    bool dirty;                  // needs re-sort

    VkBuffer gpu_buffer;         // wraps the same allocation as entries
    VkBuffer gpu_count;          // atomic counter for GPU-driven writes

    const Mel_Alloc* alloc;
};
```

**Retained lists:** entries persist until explicitly removed. ECS sync systems
use `insert` to add entries at entity creation, `get` to update in place when
components change, and `remove` to free slots on entity destruction. The GPU
buffer is always current — `OnSet` writes directly to mapped memory.

**Ephemeral lists:** backed by frame arena. Rebuilt every frame. Production
hooks and variable updates push entries each frame. Cleared on frame boundary.

Both kinds are valid graph resources. Both have GPU buffer backing.

**GPU-only lists:** lists that are only written by compute passes (never
touched by CPU) can use `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` without host
visibility. Faster GPU access. The graph infers this from topology: if a list
only appears in `write_lists` of compute passes and `read_lists` of subsequent
passes, it's GPU-only.

→ Full list API (push, get, remove, sort, sort keys): `engine.render.md`

### Render Targets

GPU images — color buffers, depth buffers, shadow maps. Written by one pass,
read by another. Intermediate nodes in the graph.

```c
typedef struct {
    u32 width, height;
    u32 format;
    u32 samples;
} Mel_Render_Target_Desc;

Mel_Render_Target* mel_render_target_create_opt(str8 name, Mel_Render_Target_Desc desc);
#define mel_render_target_create(name, ...) \
    mel_render_target_create_opt((name), (Mel_Render_Target_Desc){__VA_ARGS__})

void mel_render_target_destroy(Mel_Render_Target* target);
```

Swapchains are render targets. A window's swapchain is obtained as a target:

```c
Mel_Render_Target* mel_window_target(Mel_Window_Handle window);
```

The graph knows which targets are swapchains internally (for acquire/present).
From the pass's perspective, a swapchain is just another target you write to.

---

## Passes

A pass is a transform node in the DAG. It declares what it reads and what it
writes. Inputs and outputs can be lists or targets. The graph derives execution
order from these declarations.

```c
typedef void (*Mel_Render_Pass_Fn)(Mel_Render_Pass_Ctx* ctx);

typedef struct {
    Mel_Render_List**   read_lists;      // NULL-terminated
    Mel_Render_List**   write_lists;     // NULL-terminated
    Mel_Render_Target** read_targets;    // NULL-terminated
    Mel_Render_Target** write_targets;   // NULL-terminated
    Mel_Camera*         camera;
    Mel_Rect            viewport;
    Mel_Render_Pass_Fn  fn;
    void*               user;
    u32                 type;            // MEL_PASS_GRAPHICS (default) or MEL_PASS_COMPUTE
} Mel_Pass_Desc;
```

Convenience macros for NULL-terminated compound literals:

```c
#define MEL_LISTS(...)   ((Mel_Render_List*[]){ __VA_ARGS__, NULL })
#define MEL_TARGETS(...) ((Mel_Render_Target*[]){ __VA_ARGS__, NULL })
```

### Pass Types

- `MEL_PASS_GRAPHICS` (default): renders to targets. The graph begins a render
  pass, sets viewport/scissor, and calls `fn`. The pass function binds
  pipelines and emits draw calls.

- `MEL_PASS_COMPUTE`: transforms data. No render pass. The graph dispatches
  barriers for buffer access and calls `fn`. The pass function binds a compute
  pipeline and dispatches.

The graph uses the pass type for correct barrier stage flags:
- Graphics: `VK_PIPELINE_STAGE_*_SHADER_BIT`, color/depth attachment stages
- Compute: `VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT`, buffer barriers

### Pass Context

What the pass function receives when it runs:

```c
typedef struct {
    VkCommandBuffer     cmd;
    Mel_Render_List**   read_lists;      // resolved for this pass
    Mel_Render_List**   write_lists;
    Mel_Render_Target** read_targets;
    Mel_Render_Target** write_targets;
    Mel_Camera*         camera;
    Mel_Rect            viewport;
    void*               user;
} Mel_Render_Pass_Ctx;
```

GPU buffer access from within a pass:

```c
VkBuffer mel_pass_ctx_list_buffer(Mel_Render_Pass_Ctx* ctx, i32 list_index, bool write);
VkBuffer mel_pass_ctx_list_count_buffer(Mel_Render_Pass_Ctx* ctx, i32 list_index, bool write);
```

`write = false` indexes into `read_lists`, `write = true` indexes into
`write_lists`.

---

## Graph API

```c
Mel_Render_Graph* mel_render_graph_instance(void);    // module static

void mel_render_graph_add_pass_opt(Mel_Render_Graph* graph, str8 name, Mel_Pass_Desc desc);
#define mel_render_graph_add_pass(graph, name, ...) \
    mel_render_graph_add_pass_opt((graph), (name), (Mel_Pass_Desc){__VA_ARGS__})

void mel_render_graph_remove_pass(Mel_Render_Graph* graph, str8 name);

void mel_render_graph_compile(Mel_Render_Graph* graph);
void mel_render_graph_execute(Mel_Render_Graph* graph);
```

`compile`: builds the DAG from pass read/write declarations. Topological sort
produces execution order. Computes barrier insertion points. Plans memory
aliasing for non-overlapping transient targets. Called after topology changes
(add/remove pass). Automatically called on first execute if dirty.

`execute`: called once per frame by the engine. Acquires swapchains, resets
GPU counters for write lists, executes passes in compiled order with barriers,
presents swapchains. One call drives the entire rendering pipeline across all
windows.

---

## Graph Compilation

The graph compiles pass declarations into an execution plan:

1. **Build adjacency.** For each pass, record which resources it reads and
   writes. A write→read dependency between two passes creates an edge.

2. **Topological sort.** Derive execution order from the DAG. Cycles are
   errors (assert).

3. **Barrier insertion.** Between passes that share a resource:
   - List written by compute, read by compute/graphics → buffer memory barrier
   - Target written as color/depth attachment, read as shader input → image
     layout transition
   - CPU-populated list read by GPU pass → memory barrier after CPU writes

4. **Memory aliasing.** Transient targets with non-overlapping lifetimes can
   share the same GPU memory. The graph computes lifetimes from first-write to
   last-read and aliases where possible.

5. **Cache.** The compiled plan is cached. Recompilation only happens when
   passes are added or removed (topology change). Per-frame cost is zero for
   static topologies.

---

## GPU-Driven Rendering

The graph natively supports fully GPU-driven rendering. No special mode. No
flags. It's just a topology.

### Compute Cull + Indirect Draw

```c
Mel_Render_List*   instances  = mel_render_list_create(S8("instances"), sizeof(Mel_Mesh_Instance), alloc);
Mel_Render_List*   draw_cmds  = mel_render_list_create(S8("draw_cmds"), sizeof(VkDrawIndexedIndirectCommand), alloc);
Mel_Render_Target* hdr        = mel_render_target_create(S8("hdr"), .width = 1280, .height = 720, .format = RGBA16F);
Mel_Render_Target* depth      = mel_render_target_create(S8("depth"), .width = 1280, .height = 720, .format = D32);
Mel_Render_Target* swapchain  = mel_window_target(window);

Mel_Render_Graph* graph = mel_render_graph_instance();

mel_render_graph_add_pass(graph, S8("cull"),
    .type = MEL_PASS_COMPUTE,
    .read_lists  = MEL_LISTS(instances),
    .write_lists = MEL_LISTS(draw_cmds),
    .camera = &main_cam,
    .fn = gpu_cull_pass,
);

mel_render_graph_add_pass(graph, S8("draw"),
    .read_lists    = MEL_LISTS(draw_cmds),
    .write_targets = MEL_TARGETS(hdr, depth),
    .camera = &main_cam,
    .fn = indirect_draw_pass,
);

mel_render_graph_add_pass(graph, S8("post"),
    .read_targets  = MEL_TARGETS(hdr),
    .write_targets = MEL_TARGETS(swapchain),
    .fn = post_process_pass,
);
```

Graph compiles: cull → draw → post. Automatic. Barriers inserted between
compute buffer write and indirect draw read. Between color attachment write
and shader read.

### With LOD Selection

Insert a LOD pass before culling. Nothing else changes.

```c
Mel_Render_List* lod_resolved = mel_render_list_create(S8("lod_resolved"), sizeof(Mel_Mesh_Instance_LOD), alloc);

mel_render_graph_add_pass(graph, S8("lod_select"),
    .type = MEL_PASS_COMPUTE,
    .read_lists  = MEL_LISTS(instances),
    .write_lists = MEL_LISTS(lod_resolved),
    .camera = &main_cam,
    .fn = gpu_lod_pass,
);

mel_render_graph_add_pass(graph, S8("cull"),
    .type = MEL_PASS_COMPUTE,
    .read_lists  = MEL_LISTS(lod_resolved),
    .write_lists = MEL_LISTS(draw_cmds),
    .camera = &main_cam,
    .fn = gpu_cull_pass,
);
```

Graph compiles: LOD → cull → draw → post. LOD is per-camera (different cameras
need different LODs). The pass takes a camera, the graph handles ordering.

### Mesh Shaders

With mesh shaders, the pipeline collapses. Task shader does culling + LOD
internally. No separate compute passes needed.

```c
Mel_Render_List* meshlets = mel_render_list_create(S8("meshlets"), sizeof(Mel_Meshlet_Instance), alloc);

mel_render_graph_add_pass(graph, S8("mesh_render"),
    .read_lists    = MEL_LISTS(meshlets),
    .write_targets = MEL_TARGETS(hdr, depth),
    .camera = &main_cam,
    .fn = mesh_shader_pass,
);
```

One pass. Same input, same output. The graph doesn't care what happens inside
the pass — it just manages data flow.

Swapping between indirect draws (3 passes) and mesh shaders (1 pass) is a
topology change. Remove three passes, add one. Recompile. Done.

### Shadows

Same source list, different camera, different output.

```c
Mel_Render_Target* shadow_map = mel_render_target_create(S8("shadow_map"),
    .width = 2048, .height = 2048, .format = D32);

mel_render_graph_add_pass(graph, S8("shadow"),
    .read_lists    = MEL_LISTS(instances),
    .write_targets = MEL_TARGETS(shadow_map),
    .camera = &light_cam,
    .fn = shadow_pass,
);

mel_render_graph_add_pass(graph, S8("draw"),
    .read_lists    = MEL_LISTS(draw_cmds),
    .read_targets  = MEL_TARGETS(shadow_map),
    .write_targets = MEL_TARGETS(hdr, depth),
    .camera = &main_cam,
    .fn = indirect_draw_pass,
);
```

Shadow writes shadow_map, draw reads it. Ordering and layout transitions are
automatic.

---

## 0% CPU Steady State

Retained lists + persistently mapped GPU buffers + GPU-driven passes = zero
per-frame CPU rendering work in steady state.

1. ECS sync populates retained lists at entity creation time. `insert` writes
   directly to mapped GPU memory. Done once.

2. Every frame, the GPU reads the existing buffer. Compute passes do LOD,
   culling, indirect command generation. Draw passes render. Post-process.
   Present.

3. CPU per-frame cost: update camera UBO (~64 bytes), reset GPU counters
   (~4 bytes per write list), `vkQueueSubmit`, `vkQueuePresent`. That's it.

When something changes (entity moves, spawns, dies):
- `OnSet` → `mel_render_list_get` → direct write to mapped memory → one entry
- `OnAdd` → `mel_render_list_insert` → one new entry to mapped memory
- `OnRemove` → `mel_render_list_remove` → free the slot

CPU cost scales with **change rate**, not scene complexity. A million static
instances = zero CPU cost. Move one = one entry write.

---

## Multi-Window

Multiple windows = multiple swapchain leaves in the same graph.

```c
Mel_Render_Target* game_swap   = mel_window_target(game_win);
Mel_Render_Target* editor_swap = mel_window_target(editor_win);

mel_render_graph_add_pass(graph, S8("game_post"),
    .read_targets  = MEL_TARGETS(hdr),
    .write_targets = MEL_TARGETS(game_swap),
    .fn = post_process_pass,
);

mel_render_graph_add_pass(graph, S8("editor_view"),
    .read_lists    = MEL_LISTS(sprites, instances),
    .write_targets = MEL_TARGETS(editor_swap),
    .camera = &editor_cam,
    .fn = editor_pass,
);
```

Same source lists. Different passes. Different swapchains. One graph. One
`mel_render_graph_execute` call drives everything.

---

## ECS Sync Wiring

With the graph as a global DAG and lists as independent resources, the ECS
sync wiring question dissolves.

Sync systems write to lists. Lists exist independently. The graph routes lists
through passes to outputs. The sync system never needs to know about windows,
swapchains, or passes. It just populates the list. The graph's topology
decides who consumes it.

A world creates its default sync lists during `mel_world_create`. The game
wires the graph to route those lists to windows.

Convenience:

```c
mel_world_present_to(world, window);
```

Internally adds default passes (opaque, transparent, post-process) reading the
world's lists and writing to the window's swapchain target. Fully optional —
you can skip it and wire passes manually for complete control.

---

## Default Setup

`mel_window_create` registers the window's swapchain as a render target in the
global graph. If `mel_world_present_to(world, window)` is called, default
passes are added automatically:

```
1. Opaque Pass      — reads sprites + meshes, writes hdr + depth
2. Transparent Pass — reads transparent list, reads hdr + depth, writes hdr
3. Post-Process     — reads hdr, writes swapchain
4. UI Overlay       — reads ui list, writes swapchain
```

Game customizes by adding/removing passes:

```c
mel_render_graph_add_pass(graph, S8("shadow"), ...);
mel_render_graph_add_pass(graph, S8("particles"), ...);
mel_render_graph_remove_pass(graph, S8("post_process"));
```

The graph recompiles on topology change. Execution order updates automatically.

For maximum control, skip `mel_world_present_to` and wire everything manually.
For a raw Vulkan pass with no engine rendering, clear the graph and add one
pass:

```c
mel_render_graph_add_pass(graph, S8("raw"),
    .write_targets = MEL_TARGETS(mel_window_target(window)),
    .fn = my_raw_pass,
);
```

---

## Bindless Textures

All textures in one global descriptor array. Shaders access by index.
The bindless table is a global module static (shared across all windows).

```c
struct Mel_Bindless_Table {
    VkDescriptorSet set;
    u32* free_indices;
    u32 count;
    u32 capacity;
};
```

Texture loaded → gets bindless index. Texture unloaded → index recycled.

**The Recycling Trap:** If a render list stored a raw `u32 tex_id`, and that
texture was unloaded, the index could be recycled. The list would silently
draw the wrong texture.

**The Fix (Handle Resolution):** Render list entries store the
`Mel_Texture_Handle` (which contains a generation counter). The pass executor
resolves handles to bindless IDs right before drawing or GPU upload. If the
handle is dead or still loading, it safely substitutes `tex_missing` or
`tex_loading`'s bindless ID.

One descriptor bind per frame. No switching.

---

## Material System

Materials wrap shader + pipeline state:

- **Standard Material**: Disney PBR (albedo, normal, roughness, metalness, AO).
- **Unlit Material**: flat color / textured. UI, 2D sprites, debug.
- **Custom Material**: user shader + pipeline state.

Material ID in render list entries and sort keys. Pipeline binds correct state
per material.

---

## Open Questions

1. **Resolution independence**: can the swapchain system support rendering at
   a different resolution than the window? Useful for dynamic resolution
   scaling. Render targets with scale factors relative to swapchain size?

2. **Transient target sizing**: intermediate targets (hdr, depth) need
   dimensions. Fixed at creation? Relative to a swapchain (scale factor)?
   Auto-resize on window resize?

---

## Resolved Questions

1. ~~**Render graph barrier generation**~~: **Resolved.** Automatic. The graph
   derives barriers from pass read/write declarations and pass types
   (compute vs graphics).

2. ~~**LOD selection**~~: **Resolved.** LOD is a compute pass in the graph.
   Reads instance list, writes LOD-resolved list. Per-camera (pass takes a
   camera). The graph handles ordering. GPU-driven LOD is just a topology
   configuration, not a special mode.

3. ~~**ECS sync → render graph wiring**~~: **Resolved.** Lists are
   independent resources. Sync systems write to lists. The graph routes lists
   through passes to outputs. The sync system doesn't need to know about
   the graph. `mel_world_present_to(world, window)` wires default passes
   as a convenience.
