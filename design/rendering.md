# Rendering Design

> ARCHIVED: This document is kept as historical philosophy.
> For implementation direction use `design/engine.render.md` and `design/engine.render.graph.md`.
> Do not implement API names or data structures from this file.
> If this file conflicts with split docs, split docs always win.

## Philosophy: "AAA Power, Indie Simplicity"

The rendering engine mimics the architecture of modern AAA engines (Data-Oriented, Frame-Graph driven, Stateless) but wraps it in an immediate-mode API that feels like writing a toy engine.

**Core Tenets:**
1.  **Stateless Frontend**: The user calls `draw_sprite()` every frame. No "Sprite Objects" in the renderer.
2.  **Interpolated**: The renderer runs faster/slower than the physics. It receives `alpha` to blend states.
3.  **Bindless-Ready**: Textures are accessed by ID.

## Architecture

### 1. The Frontend (User API)
The user interacts with "Passes". A pass is a bucket of draw commands.

```c
void on_render(Mel_App* app, f32 alpha) {
    // 1. Acquire a Pass builder
    Mel_Draw_List* world_pass = mel_graph_get_pass(app, "World");
    
    // 2. Push Commands (Stateless)
    // The engine handles batching.
    // We use 'alpha' to interpolate position between 'prev' and 'curr' ECS state.
    v2 pos = v2_lerp(entity.prev_pos, entity.curr_pos, alpha);
    
    mel_draw_sprite(world_pass, pos, tex_handle, color);
    
    // 3. UI is just another pass
    Mel_Draw_List* ui_pass = mel_graph_get_pass(app, "UI");
    mel_draw_text(ui_pass, "Hello", 10, 10);
}
```

### 2. The Middle (Frame Graph)
The Engine compiles the high-level requests into a GPU-optimized execution plan.

*   **Automatic Barriers**: The graph tracks resource usage (Read/Write) and injects `vkCmdPipelineBarrier` automatically.
*   **Sorting**: 
    *   **Solid**: Front-to-Back (Z-prepass optimization).
    *   **Trans**: Back-to-Front.
    *   **UI**: Painter's Algo.

### 3. The Backend (Execution)
The graph executes via a "Executor".
*   **Multi-threaded Recording**: Independent passes record to separate Command Buffers in parallel.
*   **Bindless Resources**: All textures live in one giant descriptor array.

## Data Structures

### `Mel_Render_Packet` (The Atom)
A 64-bit key-value pair used for sorting.
*   **Key**: `[ Layer(8) | Depth(24) | Material(16) | Texture(16) ]`
*   **Value**: Index into the command data array.

### `Mel_Linear_Alloc` (Per-Frame Memory)
All render commands are allocated from the `frame_arena`. Zero fragmentation, zero free() cost.

## The Default Pipeline (AAA Defaults)

The engine ships with a "Standard Render Pipeline" configured out-of-the-box:

1.  **Z-Prepass**: Depth only. Cheap.
2.  **Opaque Pass**: Main geometry.
3.  **Skybox**: Rendered at max depth.
4.  **Transparent Pass**: Sorted back-to-front.
5.  **Post-Process**: Bloom, Tone Mapping (ACES), Gamma Correction.
6.  **UI Overlay**: ImGui / debug text.

## Material System

Instead of exposing raw Shaders/Pipelines immediately, we expose **Materials**.

*   **Standard Material**: Disney PBR (Albedo, Normal, Roughness, Metalness, AO).
*   **Unlit Material**: UI / 2D Sprites.
*   **Custom Material**: User provides raw Shader + Pipeline State.

## Why this mocks AAA?

1.  **Interpolation**: Toy engines stutter because they draw the physics state directly. This engine forces the user to acknowledge `alpha` (or ignore it), enabling 144Hz rendering on 60Hz logic.
2.  **Crash Resistance**: Using handles and "Missing" assets means the engine almost never crashes due to data errors.
3.  **Console**: A built-in CVar system (`r_show_bounds 1`) is the hallmark of a professional engine.
