# Render Composition

The layer between the application and the GPU. Technique-agnostic, scene-agnostic, API-agnostic.

## Problem

The application has data (ECS world, canvas, custom structs). The GPU produces pixels. Something needs to schedule the work: what resources exist, what passes consume and produce them, in what order, and how to manage their lifetimes.

Previous attempt (Source/View/Target/Route/Material) tried to solve scene composition and GPU scheduling simultaneously. Wrong layer. Scene composition is the application's problem. GPU scheduling is the engine's problem. This document covers GPU scheduling only.

## Three Layers

### 1. Render Graph

Passes and resources. SSA handles. Automatic ordering and aliasing.

A pass declares what it reads and what it writes. The graph derives execution order from data flow. Resources are virtual until the graph compiles them to physical GPU allocations.

This layer knows nothing about entities, transforms, sprites, meshes, cameras, or game state. It knows about images, buffers, and the passes that consume and produce them.

### 2. Passes

A pass is a function with declared inputs and outputs. It reads resources, does GPU work, writes resources. Each output is a new SSA handle. Input handles are consumed.

Engine ships passes for common operations. The user writes passes for custom operations. A pass that internally creates sub-passes is still a pass. No categories, no blessed set, no enum of techniques.

A pass picks its own implementation based on device capabilities. A "render scene" pass might use mesh shaders on hardware that supports them and fall back to indirect draw on hardware that doesn't. Same inputs, same outputs, different GPU work inside. The graph doesn't know or care.

### 3. Sync Systems

Persistent bridge between scene data and GPU resources. Not part of the render graph. Runs alongside it.

ECS observers detect changes (add, update, remove) and push deltas to GPU-side storage. The render graph consumes those storage buffers as resources. The graph never queries the ECS. It reads buffers that the sync layer keeps up to date.

Pull is also valid for simple cases (canvas immediate-mode, one-off procedural geometry). The pass doesn't care how a buffer got populated. Push and pull coexist.

## SSA Resource Handles

Every pass output is a new handle. The old handle is consumed. Data flow IS ordering.

```
r0 = window
r1 = pass_a(...) -> r0       // r0 consumed, r1 is new version
r2 = pass_b(...) -> r1       // r1 consumed, r2 is new version
present(r2)
```

Multiple outputs:

```
(albedo, normal, depth) = pass_gbuffer(scene_buf, cam)
lit = pass_lighting(albedo, normal, depth, lights)
```

A handle can be read by multiple passes. The graph tracks all readers and keeps the underlying memory alive until the last reader is done. Once all readers finish, the memory is available for aliasing.

If two passes write to the same logical destination (e.g. two layers onto the same window), the second pass takes the first pass's output as input and produces a new version:

```
r1 = pass_background(scene_bg, cam) -> r0
r2 = pass_foreground(scene_fg, cam) -> r1
present(r2)
```

Ordering comes from data flow, not declaration order, not layer integers.

## Resource Lifetime and Aliasing

The graph knows every handle's producer and all its consumers. From this it derives:

- Execution order (topological sort of the DAG)
- Resource lifetimes (first write to last read)
- Aliasing opportunities (non-overlapping lifetimes can share physical memory)
- Barriers (layout transitions, cache flushes between passes)

Transient resources (created and consumed within the graph) are allocated from a pool and reused across frames. Imported resources (swapchain images, persistent storage buffers from sync systems) have externally managed lifetimes.

## Capability-Based Pass Selection

A pass knows what GPU features it needs. At creation or graph compilation time, it checks device capabilities and selects an implementation.

Engine-provided passes ship with fallback chains:
- Mesh shaders available? Use meshlet pipeline.
- Indirect draw available? Use indirect with vertex pipeline.
- Neither? Classic draw calls.

Same pass handle. Same inputs and outputs. Different GPU codepath inside.

User-written passes choose their own policy. A custom mesh-shader-only pass can assert if the device doesn't support it. The engine doesn't enforce fallbacks on custom passes.

## Scene Data

Not a render graph concept. Scene data is owned by the application: ECS worlds, canvases, custom data structures. The render graph never sees scene data directly.

Scene data reaches the GPU through two paths:

**Push (incremental sync):** ECS observers fire on entity add/update/remove. Callbacks update slots in GPU storage pools. Only changed data is uploaded. Enables persistent GPU-side data structures required for mesh shaders, GPU-driven rendering, and large scene scalability.

**Pull (per-frame build):** A pass (or code before graph execution) queries scene data and builds a GPU buffer. Simple, good for small or fully-dynamic content. Canvas works this way.

Both paths produce GPU resources. The graph consumes those resources. The path is invisible to the graph.

## Examples

### Snake (trivial)

One pass, one window. High-level pass handles everything.

```c
Mel_Resource frame = mel_pass_render_2d(graph, scene_buf, cam);
mel_present(graph, frame, window);
```

`mel_pass_render_2d` is a pass the engine provides. Internally it creates whatever sub-passes it needs (sprite batching, text rendering). The user doesn't see or manage sub-passes.

### Split screen

Same scene buffer, two cameras, two viewports on one window.

```c
Mel_Resource left  = mel_pass_render_scene(graph, scene_buf, cam_p1, .viewport = {0, 0, 0.5, 1});
Mel_Resource right = mel_pass_render_scene(graph, scene_buf, cam_p2, .viewport = {0.5, 0, 0.5, 1});
Mel_Resource frame = mel_pass_composite(graph, left, right) -> window_r;
mel_present(graph, frame, window);
```

### Multi-window with shared work

G-buffer rendered once. Two windows get different lighting.

```c
(albedo, normal, depth) = mel_pass_gbuffer(graph, scene_buf, cam);

Mel_Resource full   = mel_pass_lighting(graph, albedo, normal, depth, all_lights);
Mel_Resource simple = mel_pass_lighting_noshadow(graph, albedo, normal, depth, all_lights);

mel_present(graph, mel_pass_tonemap(graph, full), window_main);
mel_present(graph, mel_pass_tonemap(graph, simple), window_debug);
```

Graph sees two consumers of the G-buffer outputs. Runs G-buffer once. Keeps albedo/normal/depth alive until both lighting passes finish.

### HDR post-processing

```c
Mel_Resource hdr  = mel_pass_render_scene(graph, scene_buf, cam);
Mel_Resource bloom = mel_pass_bloom(graph, hdr);
Mel_Resource ldr  = mel_pass_tonemap(graph, mel_pass_composite(graph, hdr, bloom));
Mel_Resource ui   = mel_pass_render_2d(graph, ui_buf, ui_cam);
mel_present(graph, mel_pass_composite(graph, ldr, ui), window);
```

### Custom pass (user-written)

```c
static Mel_Pass_Output my_glitch_pass(Mel_Pass_Context* ctx)
{
    Mel_Resource input = mel_pass_read(ctx, 0);
    Mel_Resource output = mel_pass_write(ctx, 0);
    // record GPU commands: bind pipeline, dispatch compute, etc.
    return mel_pass_output(output);
}

Mel_Resource scene = mel_pass_render_scene(graph, scene_buf, cam);
Mel_Resource glitched = mel_pass(graph, my_glitch_pass, .reads = {scene}, .writes = {1});
mel_present(graph, glitched, window);
```

## What This Layer Does NOT Cover

- Game state, game logic, simulation
- Input handling
- ECS world management
- Canvas API (canvas is a way to populate GPU buffers, not a graph concept)
- Stage lifecycle (init, shutdown, transitions between game modes)
- Camera management (camera is data passed to passes, not a graph concept)

These are separate concerns with their own designs.

## Open Questions

- What is the concrete pass declaration API? How does a pass declare its reads, writes, and resource requirements?
- How does `mel_present` interact with swapchain acquisition and frame pacing? Is present a special pass or a graph terminal?
- How do per-window frame cadences work? If window A runs at 144Hz and window B at 60Hz, does each window get its own graph, or one graph with conditional execution?
- What is the sync system API for registering ECS observers that push to GPU storage?
- How does the graph handle passes that need to run on specific queues (compute, transfer, graphics)?
- How does the user specify resource format and size for transient resources?
- What does the compositor pass look like internally? (Two images in, one image out — blend, alpha, additive?)
