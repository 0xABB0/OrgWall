# Melody Engine — Architecture Overview

## Vision

AAA architecture that feels like a toy engine. Zero manual subsystem init.
Multi-window capable. Async by default. Hot-reload ready.

## Status

This split design set describes the **target architecture (vNext)**.

Current implementation references:
- `melody/core.engine.h`
- `melody/core.engine.c`
- `melody/core.app.h`
- `melody/core.app.c`

## Core Design Decisions

These are non-negotiable. Every subsystem design flows from these.

**No god object. Modules own their own state.**
There is no `Mel_Engine` struct. Each module (texture pool, IO, VFS, etc.) manages its
own `static` state internally. Public APIs default to the module's static instance.
The `_opt` pattern provides overrides for testing, multi-instance, or manual control.
→ see "Module Statics & The _opt Pattern" below

**Four tiers of state: global, per-simulation, per-world, per-window.**
Global state (pools, IO, VFS, GPU device) exists once, shared across the whole process.
Per-simulation state (deterministic execution context — timing, RNG, input sourcing) is
instanced. Per-world state (ECS) is instanced. Per-window state (swapchain, render graph,
frame timing) is instanced. Simulations, worlds, and windows are all independent — the
game wires them together.
→ see "State Tiers" below

**Three independent rendering pieces.**
ECS systems sync game state into render lists (production). Render lists hold
camera-agnostic world data as persistently mapped GPU buffers (interchange).
The render graph is a global data-flow DAG — passes declare what they read and
write, the graph derives execution order, inserts barriers, and manages resource
lifetimes from the topology. Passes can output to other lists (enabling GPU-driven
culling, LOD selection) or to render targets. Swapchains are graph leaves.
Each piece is independently replaceable. They compose, they don't depend.
→ `engine.render.md` (production + interchange), `engine.render.graph.md` (consumption)

**Each asset type owns its own loading.**
No unified asset manager. `mel_load_texture` lives in the texture module.
`mel_load_font_atlas` lives in the font module. Adding a new type = adding a new file.
→ `design/engine.assets.md`

**Two async models: fibers for compute, rings for IO.**
`Mel_Job_Context` (fiber-based work stealing) for CPU-parallel work.
`Mel_Io` (SQE/CQE ring buffers with QoS lanes) for file/network IO.
Both exist, both are independent, VFS uses the IO system.
→ `design/engine.async.md`

**Nothing is forced.**
Don't want the default ECS sync systems? Replace them. Don't want the render graph?
Supply your own. Don't want render lists? Drive Vulkan directly with a single raw pass.
Every subsystem is a tool, not a chain. Every step is overridable.

---

## Module Statics & The _opt Pattern

Every module manages its own state via file-scope statics. Public convenience APIs
use the static by default. The `_opt` pattern lets callers override any dependency.

```c
typedef struct {
    Mel_Texture_Pool* pool;
    Mel_Vfs* vfs;
} Mel_Load_Texture_Opt;

Mel_Texture_Handle mel_load_texture_opt(str8 path, Mel_Load_Texture_Opt);
#define mel_load_texture(path, ...) mel_load_texture_opt((path), (Mel_Load_Texture_Opt){__VA_ARGS__})
```

Usage:

```c
mel_load_texture(S8("hero.png"));

mel_load_texture(S8("hero.png"), .pool = &my_pool);

mel_load_texture(S8("hero.png"), .pool = &test_pool, .vfs = &test_vfs);
```

NULL fields = use the module's static. This gives three usage modes:
- **Ergonomic**: zero arguments beyond the essential ones
- **Manual**: pass your own instances for custom setups
- **Testing**: pass isolated instances, no global state touched

Each module exposes init/shutdown for its static:

```c
void mel_texture_pool_init(...);
void mel_texture_pool_shutdown(void);
```

`mel_init()` calls these in the correct order. Manual users can call them individually.

---

## State Tiers

### Global (Module Statics) — one instance, shared by everything

- `Mel_Tracking_Allocator` — allocation stats
- `Mel_Job_Context` — fiber-based compute workers
- `Mel_Io` — ring-based IO workers
- `Mel_Vfs` + `Mel_Vfs_Backend*` — virtual filesystem
- `Mel_Cvar_Registry` — runtime configuration
- `Mel_Gpu_Device` — Vulkan device (one per process)
- `Mel_Bindless_Table` — global texture descriptor array
- `Mel_Texture_Pool`, `Mel_Font_Atlas_Pool`, `Mel_Tileset_Pool`, `Mel_Tilemap_Pool`
- `Mel_Texture_Handle` tex_missing, tex_loading, tex_white — default assets
- `Mel_Input_Map*` — action map registry (global)

### Per-Simulation (Instanced) — the central execution context

Partially implemented: `melody/sim.ctx.h`, `melody/sim.ctx.c`.
Scheduling, fixed contexts, registration, and time scaling are target (vNext).

The simulation (`Mel_Sim_Ctx`) is the engine's equivalent of a "scene" —
but lighter. It doesn't own entities (worlds are separate), doesn't own
assets (pools are global), doesn't own rendering (render graphs are
per-window). It's the **execution context** that ties those independent
pieces together into a coherent thing that runs.

All game logic flows through a simulation. Inputs are routed to simulations.
Simulations drive fixed update contexts and variable updates. The engine
ticks all registered simulations each frame.

A simulation does NOT own a world. A simulation does NOT know about windows.
The game wires simulation contexts to worlds and windows.

Use cases: gameplay, replays (hardcoded inputs + seeded RNG = deterministic),
automated testing (replay a known sequence, assert outcomes), title screen
demos (Doom-style attract mode), loading screens, pause menus.

```c
struct Mel_Sim_Ctx {
    Mel_Rng rng;                    // seeded, deterministic PRNG
    u64 tick;                       // tick counter (bumped on clear)
    Mel_Block_Alloc events;         // typed event buffer (push/iterate/clear per tick)

    Mel_Sim_Fixed** fixed_contexts; // dynamic array of fixed update contexts
    Mel_Sim_Variable* variables;    // dynamic array of variable updates
    f32 time_scale;                 // 1.0 normal, 0.0 paused, 2.0 fast-forward

    void* user;                     // scene-level user data

    Mel_Sim_Ctx* next;              // intrusive list for engine registration
    bool registered;
};
```

The simulation context is caller-allocated (not a handle). The game creates
as many as it needs:

```c
typedef struct {
    u64 seed;
    void* event_buffer;
    size event_buffer_size;
    void* user;
} Mel_Sim_Opt;

void mel_sim_init_opt(Mel_Sim_Ctx* ctx, Mel_Sim_Opt);
#define mel_sim_init(ctx, ...) mel_sim_init_opt((ctx), (Mel_Sim_Opt){__VA_ARGS__})
```

```c
static Mel_Sim_Ctx sim;
static u8 event_buf[4096];

void app_init(void) {
    mel_sim_init(&sim, .seed = 42, .user = &game_state,
        .event_buffer = event_buf, .event_buffer_size = sizeof(event_buf));

    Mel_Sim_Fixed* physics = mel_sim_add_fixed(&sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(physics, gameplay_tick, .user = &world);
    mel_sim_add_variable(&sim, push_sprites);

    mel_sim_register(&sim);
}
```

→ Full frame pipeline, fixed contexts, variable updates, alpha,
  deferred mutations, and time scaling: `design/engine.frame.md`

### Per-World (Instanced) — entity storage

```c
Mel_World_Handle mel_world_create(void);
void mel_world_destroy(Mel_World_Handle handle);
ecs_world_t* mel_world_ecs(Mel_World_Handle handle);
```

A world is ECS entity storage — components, systems, queries. It does not
know about simulations, windows, cameras, or timing.

The default ECS sync systems (which produce render list entries from game
components) are registered on a world when it's created. Replace them by
removing the defaults and registering your own.

Worlds and simulations are independent. The game connects them:

```c
static Mel_Sim_Ctx sim;
static u8 event_buf[4096];
static Mel_World_Handle world;

void gameplay_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    mel_sim_push(sim, EVT_INPUT, &input, sizeof(input));

    ecs_world_t* ecs = mel_world_ecs(world);
    ecs_progress(ecs, dt);
}
```

A simulation context can drive zero, one, or many worlds. A world can exist
without a simulation (editor, static scene). Multiple simulation contexts
can drive separate worlds (main game + title screen demo running
simultaneously).

### Per-Window (Instanced) — one per window

```c
struct Mel_Window {
    void* platform_handle;
    Mel_Swapchain swapchain;
    Mel_Render_Frame frame;
    Mel_Render_Target* target;   // swapchain as a render target (graph leaf)
    Mel_Input_State input;

    Mel_Linear_Alloc arenas[MEL_MAX_FRAMES_IN_FLIGHT]; // N-buffered for render frames

    f32 frame_dt;
    u64 last_time, frame_count;
    bool resize_requested;
};
```

Each window has its own swapchain, exposed as a `Mel_Render_Target*` to the
global render graph. The render graph is NOT per-window — it is a global
data-flow DAG. Windows are leaves (swapchain targets) in the graph.
`mel_window_target(window)` returns the target for use in pass declarations.

Input state is per-window (which window has focus, key states for that window).
The action map registry is global (bindings are shared), but input polling is per-window.

---

→ App entry, lifecycle, init/shutdown ordering: `engine.app.md`

---

## Subsystem Design Documents

- `engine.app.md` — App entry (current + target), lifecycle, init/shutdown ordering, manual path, headless
- `engine.sim.md` — Simulation identity, registration, time scaling, event lifecycle, user data, scene lifecycle
- `engine.frame.md` — Frame pipeline, fixed update contexts, variable updates, alpha/interpolation, deferred mutations
- `engine.render.md` — Render lists (production + interchange), ECS sync systems, draw API, manual submission
- `engine.render.graph.md` — Global data-flow DAG, passes (read/write declarations, compute + graphics), GPU-driven rendering (LOD, cull, indirect draw, mesh shaders), bindless textures, materials
- `engine.assets.md` — Per-type pools, async loading with fallbacks, progress tracking
- `engine.fonts.md` — Font descriptor, atlas/SDF/MSDF rendering modules, adding new techniques
- `engine.async.md` — Job system, IO system, frame arena, threading model, VFS integration
- `vfs.md` — VFS architecture, backends, mount resolution, async contract
- `vfs.status.md` — VFS implementation tracking, per-file notes, tests, execution history
