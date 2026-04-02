# Render Composition

The layer between the application and the GPU. Technique-agnostic, scene-agnostic, API-agnostic.

## The Layer Cake

Rendering in Melody is a stack of languages. Each layer is implemented in terms of the layer below it. The user enters at whatever layer matches their need. Dropping down one level is always possible and always gives you more control.

```
Layer 4: Scene        "draw this world"          speaks Layer 3
Layer 3: Material     "configure how to draw"    speaks Layer 2
Layer 2: Pipeline     "define a way to draw"     speaks Layer 1
Layer 1: GPU          "record GPU commands"      speaks Vulkan/Metal
```

Layer 4 (Scene) is code written in Layer 3's language. The scene forward pass internally creates materials, binds them, and issues draw calls. If you read its source, you see the same API you'd use to write a custom renderer.

Layer 3 (Material) is code written in Layer 2's language. A material wraps a pipeline with its configuration: shader, blend mode, textures, push constant layout. Creating a new material type means building a pipeline from shaders and render state.

Layer 2 (Pipeline) is code written in Layer 1's language. A pipeline is a configured GPU state object built from `gpu.*` calls: shader modules, vertex layouts, descriptor sets, render state.

Layer 1 (GPU) is the command recording layer: `mel_gpu_cmd_bind_pipeline`, `mel_gpu_cmd_draw_indexed`, `mel_gpu_cmd_push_constants`. Thin wrappers around the graphics API.

There are no shortcuts that skip layers. Scene speaks material. Material speaks pipeline. Pipeline speaks GPU. If you need more control, you drop ONE level — not two. This means:

- Writing a custom renderer = using materials + draw calls (Layer 3)
- Writing a custom material = building a pipeline from shaders (Layer 2)
- Writing a custom pipeline feature = recording GPU commands (Layer 1)
- Writing a custom GPU backend = talking to Vulkan/Metal directly

The engine's built-in renderers are not special. They use the same materials, the same pipelines, the same GPU commands that user code has access to. There is no blessed path.

## Materials

A material is a shader + render state + resource bindings. It is THE language for "configure how things get drawn." Not a convenience wrapper — the core vocabulary.

The engine ships a small set of shaders. Materials combine a shader with render state (blend mode, depth, format) to produce a concrete pipeline. No variant explosion — what you create is what exists.

```c
Mel_Material mat = mel_material(
    .shader = mel_shader_flat2d(),
    .blend = MEL_GPU_BLEND_ALPHA,
    .format = mel_window_swapchain_format(win),
);
```

One material = one pipeline. The engine provides shaders (`mel_shader_flat2d`, `mel_shader_sprite2d`, `mel_shader_pbr`), the user selects and configures. Custom shaders work the same way — load your own and pass it in.

`mel_material_bind` binds the pipeline + pushes material-specific state (textures, uniforms) in one call. The user still issues draw commands with `mel_gpu_cmd_*`. Materials configure the GPU, the user controls what gets drawn.

## Render Graph

Passes and resources. SSA handles. Automatic ordering and aliasing.

A pass declares what it reads and what it writes. The graph derives execution order from data flow. Resources are virtual until the graph compiles them to physical GPU allocations.

This layer knows nothing about entities, transforms, sprites, meshes, cameras, or game state. It knows about images, buffers, and the passes that consume and produce them.

### Passes

A pass is a function with declared inputs and outputs. It reads resources, does GPU work, writes resources. Each output is a new SSA handle. Input handles are consumed.

Engine ships passes for common operations. The user writes passes for custom operations. Both are functions that speak the material language (Layer 3). A pass that internally creates sub-passes is still a pass. No categories, no blessed set, no enum of techniques.

A pass picks its own implementation based on device capabilities. A "render scene" pass might use mesh shaders on hardware that supports them and fall back to indirect draw on hardware that doesn't. Same inputs, same outputs, different GPU work inside. The graph doesn't know or care.

### Sync Systems

Persistent bridge between scene data and GPU resources. Not part of the render graph. Runs alongside it.

ECS observers detect changes (add, update, remove) and push deltas to GPU-side storage. The render graph consumes those storage buffers as resources. The graph never queries the ECS. It reads buffers that the sync layer keeps up to date.

Pull is also valid for simple cases (one-off procedural geometry, per-frame rebuilds). The pass doesn't care how a buffer got populated. Push and pull coexist.

## Two Kinds of Resources

### Transient

Created and consumed within the graph. The graph allocates physical memory, tracks lifetime (first write to last read), and reuses memory via aliasing when lifetimes don't overlap. The user never manages these — the graph handles everything.

### Imported

Exist outside the graph. The graph manages their state (layout transitions, barriers) but not their lifetime or memory. Examples: swapchain images, persistent GPU storage buffers from sync systems, textures loaded from disk.

Writing to an imported resource is a side-effect. The graph's culling algorithm uses this: any pass that writes to an imported resource survives culling, and everything that feeds into it survives by reference count propagation. This is how the graph knows what work matters without knowing what the resource is for.

## SSA Resource Handles

Every pass output is a new handle. The old handle is consumed. Data flow IS ordering.

```
r0 = import(backbuffer)
r1 = pass_a(...) -> r0       // r0 consumed, r1 is new version
r2 = pass_b(...) -> r1       // r1 consumed, r2 is new version
```

Multiple outputs:

```
(albedo, normal, depth) = pass_gbuffer(scene_buf, cam)
lit = pass_lighting(albedo, normal, depth, lights)
```

A handle can be read by multiple passes. The graph tracks all readers and keeps the underlying memory alive until the last reader is done. Once all readers finish, the memory is available for aliasing.

If two passes write to the same logical destination (e.g. two layers composited), the second pass takes the first pass's output as input and produces a new version:

```
r0 = import(backbuffer)
r1 = pass_background(scene_bg, cam) -> r0
r2 = pass_foreground(scene_fg, cam) -> r1
```

r2 is the final version of the backbuffer. The graph sees it has no consumers, it's an imported resource — side-effect pass, survives culling. Ordering comes from data flow, not declaration order, not layer integers.

## Frame Lifecycle

The graph does NOT know about presentation. Present is a frame lifecycle concern handled by the engine's frame coordinator. Every engine in the industry does this the same way:

```
1. Acquire swapchain image(s)
2. Import backbuffer(s) into graph as external resources
3. Build graph (passes declare reads/writes)
4. Compile graph (culling, ordering, aliasing, barriers)
5. Execute graph (GPU work)
6. Present swapchain image(s)
```

The graph only does steps 3-5. Steps 1, 2, and 6 are the frame coordinator's job.

The swapchain image enters the graph as an imported resource. The graph writes to it like any other resource. The physical backing is swapped each frame (the logical handle stays the same, the underlying image changes after acquire). The graph's final layout transition for the backbuffer resource targets `PRESENT_SRC` — but the graph doesn't know why. It just applies the layout the resource was imported with.

This separation means:
- The graph is pure GPU scheduling. No display timing, no vsync, no frame pacing.
- The frame coordinator handles all swapchain synchronization (acquire semaphores, present semaphores, fences).
- Multiple windows = multiple imported resources, each with its own acquire/present cycle.
- The graph can be rebuilt from scratch every frame (Filament) or reused with swapped physical backing (Granite).

## Resource Lifetime and Aliasing

The graph knows every handle's producer and all its consumers. From this it derives:

- Execution order (topological sort of the DAG)
- Resource lifetimes (first write to last read)
- Aliasing opportunities (non-overlapping lifetimes can share physical memory)
- Barriers (layout transitions, cache flushes between passes)

Culling: reference-count flood-fill. Passes that write to imported resources are side-effect passes (refcount never hits zero). Everything feeding into them stays alive. Everything else gets culled.

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

### Snake (trivial — custom pass with materials)

One custom pass writes colored quads to the window backbuffer. The user owns vertex data and draw calls. A material handles the pipeline. The graph handles the frame.

```c
static void snake_render(Mel_Pass_Context* ctx, void* user)
{
    Snake* s = user;
    s->quad_count = 0;

    for (i32 x = 0; x <= GW; x++)
        push_quad(s, x * CELL, 0, 1, GH * CELL, COL_GRID);
    for (i32 y = 0; y <= GH; y++)
        push_quad(s, 0, y * CELL, GW * CELL, 1, COL_GRID);

    for (i32 i = 0; i < s->len; i++) {
        f32 p = i == 0 ? 1 : 2;
        push_quad(s, s->snake[i].x * CELL + p, s->snake[i].y * CELL + p,
                  CELL - p*2, CELL - p*2, i == 0 ? COL_HEAD : COL_BODY);
    }
    push_quad(s, s->food.x * CELL + 4, s->food.y * CELL + 4, CELL - 8, CELL - 8, COL_FOOD);

    mel_gpu_buffer_upload(&s->vbo, ctx->dev, s->verts, s->quad_count * 4 * sizeof(Quad_Vert), 0);

    Mel_Mat4 proj = mel_mat4_ortho(0, GW * CELL, GH * CELL, 0, -1, 1);
    mel_material_bind(ctx->cmd, s->mat, .projection = proj);
    mel_gpu_cmd_bind_vertex_buffer(ctx->cmd, &s->vbo, 0);
    mel_gpu_cmd_bind_index_buffer(ctx->cmd, mel_quad_ibo(), 0, MEL_GPU_INDEX_TYPE_U16);
    mel_gpu_cmd_draw_indexed(ctx->cmd, s->quad_count * 6, 1, 0, 0, 0);
}
```

The render function receives a `Mel_Pass_Context*` with a command buffer and device. It builds quads on the CPU, uploads to a vertex buffer, binds a material (which handles the pipeline), and draws. Same `mel_gpu_cmd_*` functions the engine uses internally.

Setup:

```c
s->mat = mel_material(.shader = mel_shader_flat2d(), .blend = MEL_GPU_BLEND_ALPHA,
                       .format = mel_window_swapchain_format(win));
mel_gpu_buffer_init(&s->vbo, dev, .size = MAX_QUADS * 4 * sizeof(Quad_Vert),
                    .usage = MEL_GPU_BUFFER_USAGE_VERTEX,
                    .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);

Mel_Graph graph = mel_graph();
Mel_Render_Resource frame = mel_window_backbuffer(win, graph);
frame = mel_pass(graph, snake_render, s, .colors = {{
    .image = frame, .load_op = MEL_GPU_LOAD_OP_CLEAR, .store_op = MEL_GPU_STORE_OP_STORE,
    .clear_r = COL_BG.x, .clear_g = COL_BG.y, .clear_b = COL_BG.z, .clear_a = COL_BG.w,
}});
```

Material: one line. Vertex buffer: one line. No shader loading, no pipeline creation, no index buffer generation. `mel_quad_ibo()` is a shared engine-owned quad index buffer. `mel_shader_flat2d()` is a pre-compiled engine shader. The material wraps both into a pipeline the user binds with one call.

The same attachment model scales to full MRT:

```c
(albedo, normal, material, depth) = mel_pass_gbuffer(graph, scene, cam, .colors = {
    {.format = MEL_FMT_RGBA8},
    {.format = MEL_FMT_RGBA16F},
    {.format = MEL_FMT_RGBA8},
}, .depth = {
    .format = MEL_FMT_D32F,
});
```

Snake stays lean because it only binds one color target, but the API surface is already plural and directly maps to passes that write several outputs in one go.

### Split screen

Same scene buffer, two cameras, two viewports on one window.

```c
Mel_Render_Resource screen = mel_window_backbuffer(win, graph);
Mel_Render_Resource left  = mel_pass_render_scene(graph, scene_buf, cam_p1, .viewport = {0, 0, 0.5, 1});
Mel_Render_Resource right = mel_pass_render_scene(graph, scene_buf, cam_p2, .viewport = {0.5, 0, 0.5, 1});
Mel_Render_Resource frame = mel_pass_composite(graph, left, right, .onto = screen);
```

### Multi-window with shared work

G-buffer rendered once. Two windows get different lighting. Each window has its own imported backbuffer.

```c
Mel_Render_Resource screen_main  = mel_window_backbuffer(window_main, graph);
Mel_Render_Resource screen_debug = mel_window_backbuffer(window_debug, graph);

(albedo, normal, depth) = mel_pass_gbuffer(graph, scene_buf, cam);

Mel_Render_Resource full   = mel_pass_lighting(graph, albedo, normal, depth, all_lights);
Mel_Render_Resource simple = mel_pass_lighting_noshadow(graph, albedo, normal, depth, all_lights);

Mel_Render_Resource r0 = mel_pass_tonemap(graph, full, .onto = screen_main);
Mel_Render_Resource r1 = mel_pass_tonemap(graph, simple, .onto = screen_debug);
```

Graph sees two imported resources written to. Both survive culling. G-buffer passes survive because they feed into the lighting passes which feed into the tonemaps. Graph runs G-buffer once, keeps outputs alive until both consumers finish.

The frame coordinator acquires both swapchains, executes the graph, presents both.

### HDR post-processing

```c
Mel_Render_Resource screen = mel_window_backbuffer(win, graph);
Mel_Render_Resource hdr   = mel_pass_render_scene(graph, scene_buf, cam);
Mel_Render_Resource bloom  = mel_pass_bloom(graph, hdr);
Mel_Render_Resource ldr   = mel_pass_tonemap(graph, mel_pass_composite(graph, hdr, bloom));
Mel_Render_Resource ui    = mel_pass_render_2d(graph, ui_buf, ui_cam);
Mel_Render_Resource frame = mel_pass_composite(graph, ldr, ui, .onto = screen);
```

### Custom pass (user-written, Layer 3)

A custom pass speaks the material language. It binds materials, uploads vertex data, issues draw calls. Same vocabulary the scene pass uses internally.

```c
static void my_particles(Mel_Pass_Context* ctx, void* user)
{
    Particle_System* ps = user;
    mel_gpu_buffer_upload(&ps->vbo, ctx->dev, ps->verts, ps->count * sizeof(Particle_Vert), 0);

    mel_material_bind(ctx->cmd, ps->mat, .projection = ps->proj, .texture = ps->atlas);
    mel_gpu_cmd_bind_vertex_buffer(ctx->cmd, &ps->vbo, 0);
    mel_gpu_cmd_draw(ctx->cmd, ps->count, 1, 0, 0);
}

Mel_Render_Resource screen = mel_window_backbuffer(win, graph);
Mel_Render_Resource scene = mel_pass_render_scene(graph, scene_buf, cam);
Mel_Render_Resource frame = mel_pass(graph, my_particles, ps, .colors = {{
    .image = scene, .load_op = MEL_GPU_LOAD_OP_LOAD, .store_op = MEL_GPU_STORE_OP_STORE,
}});
frame = mel_pass_blit(graph, frame, .onto = screen);
```

### Custom material (user-written, Layer 2)

When the engine's shaders don't cover your use case, you drop one level and build a material from a custom shader + pipeline. This is Layer 2: you speak the pipeline language.

```c
Mel_Gpu_Shader shader;
mel_gpu_shader_load(&shader, .path = S8("shaders/my_effect.slang"), .dev = dev);

Mel_Material mat = mel_material(
    .shader = &shader,
    .blend = MEL_GPU_BLEND_ADD,
    .depth_test = true,
    .format = mel_window_swapchain_format(win),
);
```

The resulting material is used identically to engine-provided materials. `mel_material_bind` works the same way. Pass code doesn't know or care whether the material uses a built-in or custom shader.

## Multi-Window Strategies

Research across Frostbite, Filament, Unreal, Bevy, and Granite shows three patterns:

**One graph per window, independent frame loops.** Best for windows at different refresh rates. Each window has its own acquire/present cycle, its own graph instance. Shared GPU resources (scene buffers, textures) are imported into both graphs.

**One graph, multiple imported backbuffers.** All windows share one frame cadence. The graph writes to multiple imported resources. The frame coordinator acquires all swapchains, executes the graph once, presents all. Simpler but locks windows to the same refresh rate.

**One primary graph + blit to secondary windows.** Main window drives the graph. Secondary windows (debug, inspector) get a blit of some intermediate resource. Asymmetric but practical.

The choice depends on the application. The graph abstraction supports all three because it doesn't know about presentation — it just writes to imported resources.

## Industry References

This architecture is validated by research across five major engines/frameworks:

- **Frostbite** (GDC 2017, Yuriy O'Donnell): Frame graph with imported/transient resources. Backbuffer is imported. Side-effect culling. Present outside graph.
- **Filament** (Google): Ephemeral frame graph per frame. Backbuffer imported via `fg.import()`. Present in `endFrame()` via `swapChain->commit()`. Graph never touches swapchain.
- **Unreal Engine 5**: RDG handles rendering. Backbuffer enters via `RegisterExternalTexture()`. Present at RHI viewport layer (`RHIEndDrawingViewport`). Per-OS-window compositing via Slate.
- **Bevy/wgpu**: Acquire in `prepare_windows` before graph. Graph renders into ViewTargets. Present in `render_system` after graph. No present node.
- **Granite** (Vulkan): `set_backbuffer_source()` marks terminal resource. Physical backing swapped to swapchain image at setup. Zero-copy when dimensions match.

All five separate the graph from presentation. The graph is pure GPU scheduling. Present is frame lifecycle.

## Open Questions

- What is the concrete `Mel_Pass_Context` struct? At minimum: `Mel_Gpu_Cmd* cmd`, `Mel_Gpu_Device* dev`, `u32 width`, `u32 height`, `u32 frame_index`. What else?
- What does `mel_material_bind` actually do? Binds pipeline + sets push constants + binds descriptor sets? What's the push constant convention — projection always at offset 0?
- How does text rendering integrate? Same material with `.texture = font_atlas`? Or a separate text material type?
- How does the frame coordinator manage multiple swapchains with different refresh rates? Per-window frame loops or shared cadence with independent present timing?
- What is the sync system API for registering ECS observers that push to GPU storage?
- How does the graph handle passes that need to run on specific queues (compute, transfer, graphics)?
- How does the user specify resource format and size for transient resources?
- What does the compositor pass look like internally? (Two images in, one image out — blend, alpha, additive?)
- Should the graph be rebuilt from scratch every frame (Filament model) or compiled once and reused with swapped physical backing (Granite model)?
- What engine shaders ship by default? `mel_shader_flat2d` (pos+color), `mel_shader_sprite2d` (pos+uv+color), `mel_shader_pbr` — what else?
- How does the material handle per-frame vs per-material push constants? Projection changes per frame, blend mode is fixed at creation. What's the split?
