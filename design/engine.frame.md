# Frame Pipeline

## Status

This document describes the target frame architecture (vNext).
No implementation exists yet for the scheduling / fixed context system.
`Mel_Sim_Ctx` (deterministic state: RNG, tick, events) is already
implemented in `melody/sim.ctx.h`, `melody/sim.ctx.c`.

---

## What Happens Each Frame

```
mel_poll_events()

mel__tick_simulations()
    for each registered simulation (registration order):
        for each fixed context (registration order):
            accumulator += frame_dt * sim.time_scale
            while accumulator >= fixed_dt:
                for each update in context (registration order):
                    call(sim, fixed_dt, user)
                accumulator -= fixed_dt
                fixed.tick++
            compute this context's alpha
        for each variable update (registration order):
            call(sim, frame_dt * sim.time_scale, user)
        clear sim events, bump sim.tick
    apply deferred mutations

mel__drive_windows()
    for each window:
        begin_frame → call producers → sort lists → execute graph → end_frame
```

Three phases. Events first. Then all simulations tick (including automatic
event clearing). Then all windows render. No interleaving between phases.

---

→ Simulation identity, registration, time scaling, event lifecycle,
  user data, scene lifecycle: `engine.sim.md`

---

## Fixed Update Contexts

A fixed update context (`Mel_Sim_Fixed`) owns a single accumulator at a
specific rate. Multiple updates share the same accumulator — they all fire
together each tick. Alpha is per-context.

### Why Contexts?

Physics at 60Hz and AI at 10Hz need separate accumulators. But `physics_tick`
and `collision_tick` at the same 60Hz should share one accumulator — they're
part of the same logical tick. They fire together, in order, once per tick.

Grouping by context gives:
- One accumulator per rate (not per callback)
- Same-rate callbacks fire together each tick
- Alpha is unambiguous — query the context you care about
- Moving callbacks between contexts is just remove + add

### Data

```c
struct Mel_Sim_Fixed {
    f32 fixed_dt;
    f32 accumulator;
    f32 alpha;
    u64 tick;                   // how many times this context has ticked (total, not per-frame)
    Mel_Sim_Update* updates;    // dynamic array
};
```

Managed by the simulation. Created via `mel_sim_add_fixed`, destroyed when
the sim is torn down or via `mel_sim_remove_fixed`.

`fixed.tick` counts total accumulator fires — it's the fixed context's own
frame-rate-independent counter. Use this for replay indexing, deterministic
sequencing, or anything that needs to count at the fixed rate.

### API

```c
typedef struct {
    f32 fixed_dt;
} Mel_Sim_Add_Fixed_Opt;

Mel_Sim_Fixed* mel_sim_add_fixed_opt(Mel_Sim_Ctx* sim, Mel_Sim_Add_Fixed_Opt);
#define mel_sim_add_fixed(sim, ...) mel_sim_add_fixed_opt((sim), (Mel_Sim_Add_Fixed_Opt){__VA_ARGS__})

void mel_sim_remove_fixed(Mel_Sim_Ctx* sim, Mel_Sim_Fixed* fixed);
```

### Adding Updates to a Fixed Context

```c
typedef void (*Mel_Update_Fn)(Mel_Sim_Ctx* sim, f32 dt, void* user);

typedef struct {
    void* user;
} Mel_Fixed_Add_Update_Opt;

Mel_Update_Handle mel_sim_fixed_add_update_opt(Mel_Sim_Fixed* fixed, Mel_Update_Fn fn, Mel_Fixed_Add_Update_Opt);
#define mel_sim_fixed_add_update(fixed, fn, ...) mel_sim_fixed_add_update_opt((fixed), (fn), (Mel_Fixed_Add_Update_Opt){__VA_ARGS__})

void mel_sim_fixed_remove_update(Mel_Sim_Fixed* fixed, Mel_Update_Handle handle);
```

### Alpha (Interpolation)

```c
f32 mel_sim_fixed_alpha(Mel_Sim_Fixed* fixed);
```

Alpha = `accumulator / fixed_dt` after all ticks for this context have run.
Ranges from 0.0 (just ticked) to < 1.0 (almost at next tick).

Rendering code queries the alpha of the fixed context that drives the
visual state it's interpolating. Physics alpha for position interpolation.
AI alpha is irrelevant to rendering — AI decisions are discrete, not
interpolated.

```c
void push_sprites(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    f32 alpha = mel_sim_fixed_alpha(physics);
    Mel_Vec2 draw_pos = mel_vec2_lerp(prev_pos, pos, alpha);
    // ...
}
```

---

## Variable Updates

Per-simulation. Run once per frame after all fixed contexts have ticked.
Receive the frame delta (scaled by `sim.time_scale`).

```c
typedef struct {
    void* user;
} Mel_Sim_Add_Variable_Opt;

Mel_Update_Handle mel_sim_add_variable_opt(Mel_Sim_Ctx* sim, Mel_Update_Fn fn, Mel_Sim_Add_Variable_Opt);
#define mel_sim_add_variable(sim, fn, ...) mel_sim_add_variable_opt((sim), (fn), (Mel_Sim_Add_Variable_Opt){__VA_ARGS__})

void mel_sim_remove_variable(Mel_Sim_Ctx* sim, Mel_Update_Handle handle);
```

Same callback signature as fixed updates: `void (*)(Mel_Sim_Ctx* sim, f32 dt, void* user)`.
The difference is when they run and what `dt` means (real frame delta vs fixed timestep).

Variable updates are where rendering happens — query alpha from a fixed
context, interpolate state, push to render lists.

---

## Deferred Mutations

All structural changes are deferred until after ALL simulations have
finished their tick phase for the current frame:

- `mel_sim_register` / `mel_sim_unregister`
- `mel_sim_add_fixed` / `mel_sim_remove_fixed`
- `mel_sim_fixed_add_update` / `mel_sim_fixed_remove_update`
- `mel_sim_add_variable` / `mel_sim_remove_variable`

This means:
- Removing an update from inside a callback: the update may still run
  this frame if it hasn't been reached yet
- Adding an update from inside a callback: it starts running next frame
- Unregistering a sim from inside its own callback: it finishes this
  frame's ticks, then is removed

Changes are queued and applied between `mel__tick_simulations()` and
`mel__drive_windows()`.

All mutation APIs are main-thread only.

---

## Typical Setup

```c
static Mel_Sim_Ctx sim;
static u8 event_buf[4096];
static Mel_Sim_Fixed* physics;
static Mel_World_Handle world;
static Mel_Window_Handle window;

void physics_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Mel_World_Handle world = *(Mel_World_Handle*)user;
    ecs_world_t* ecs = mel_world_ecs(world);

    Mel_Sim_Iter iter = {0};
    Move_Event* move;
    while ((move = mel_sim_next(sim, EVT_MOVE, &iter)) != NULL) {
        // apply movement ...
    }

    ecs_progress(ecs, dt);
}

void push_sprites(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    f32 alpha = mel_sim_fixed_alpha(physics);
    Mel_Vec2 draw_pos = mel_vec2_lerp(prev_pos, pos, alpha);

    Mel_Render_Graph* graph = mel_window_graph(window);
    Mel_Render_List* sprites = mel_render_graph_get_list(graph, S8("sprites"));
    Mel_Sprite_Entry* e = mel_render_list_push(sprites, 0);
    *e = (Mel_Sprite_Entry){ .pos = draw_pos, .tex = hero, .color = 0xFFFFFFFF, ... };
}

void app_init(void) {
    mel_vfs_mount(S8("assets"), mel_vfs_backend_os());
    window = mel_window_create(S8("Game"), 1280, 720);
    world = mel_world_create();

    mel_sim_init(&sim, .seed = 42, .user = &game,
        .event_buffer = event_buf, .event_buffer_size = sizeof(event_buf));

    physics = mel_sim_add_fixed(&sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(physics, physics_tick, .user = &world);
    mel_sim_add_variable(&sim, push_sprites);

    mel_sim_register(&sim);
}

void app_shutdown(void) {
    mel_sim_unregister(&sim);
    mel_world_destroy(world);
    mel_window_destroy(window);
}
```

---

## Open Questions

1. **Fixed context naming**: `Mel_Sim_Fixed` is the working name. Alternatives:
   `Mel_Sim_Phase`, `Mel_Tick_Group`, `Mel_Sim_Step`. Need to settle on one.

2. **Fixed context allocation**: currently engine-managed (returned pointer,
   owned by the sim). Alternative: caller-allocated like `Mel_Sim_Ctx` itself.
   Engine-managed is simpler for most cases. Caller-allocated gives more
   control for testing.
