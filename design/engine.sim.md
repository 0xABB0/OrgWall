# Simulation Context

## Status

This document describes the target simulation architecture (vNext).
`Mel_Sim_Ctx` (deterministic state: RNG, tick, events) is already
implemented in `melody/sim.ctx.h`, `melody/sim.ctx.c`.
Scheduling, fixed contexts, registration, and time scaling are target (vNext).

→ How the frame pipeline drives simulations: `engine.frame.md`
→ `Mel_Sim_Ctx` struct and init API: `engine.overview.md` "State Tiers > Per-Simulation"

---

## What Is a Simulation?

The simulation (`Mel_Sim_Ctx`) is the engine's central execution
context — the equivalent of a "scene" in other engines, but lighter.
It doesn't own entities (worlds are separate), doesn't own assets
(pools are global), doesn't own rendering (render graphs are per-window).
It's the organizational unit that ties independent pieces together into
something that runs.

All game logic flows through a simulation. Inputs are routed to
simulations (via `mel_sim_push`). Simulations tick their updates.
Updates drive worlds and populate render lists.

### Registration

```c
void mel_sim_register(Mel_Sim_Ctx* sim);
void mel_sim_unregister(Mel_Sim_Ctx* sim);
```

The engine maintains a linked list of registered simulations (intrusive
list via `Mel_Sim_Ctx.next`). Registration order = execution order.
Zero registered simulations is valid — windows still render, nothing ticks.

### Time Scale

```c
sim.time_scale = 1.0f;   // normal
sim.time_scale = 0.5f;   // slow motion
sim.time_scale = 2.0f;   // fast forward
sim.time_scale = 0.0f;   // paused — no fixed ticks fire, variable updates still run with dt=0
```

Time scale affects the accumulator delta for all fixed contexts in the sim:
`accumulator += frame_dt * sim.time_scale`. Variable updates receive
`frame_dt * sim.time_scale` as their delta.

When paused (`time_scale = 0`), accumulators don't advance (no fixed ticks),
but variable updates still fire with `dt = 0`. This lets the sim still
render its current state (pause screen overlay, etc.). If you want a fully
frozen sim, unregister it.

---

## Event Lifecycle

Events pushed via `mel_sim_push` are available to ALL updates (fixed and
variable) during the entire tick phase for that sim. The engine clears
events automatically after the sim's tick phase completes. `sim->tick`
is bumped once per frame (counts event cycles).

The game does NOT call `mel_sim_clear`. The engine handles it.

```
sim tick phase:
    game pushes events (mel_sim_push)
    → all fixed contexts tick (events visible to all)
    → all variable updates run (events still visible)
    → engine clears events, bumps sim->tick
```

This means:
- Events pushed before or during fixed ticks are visible to all contexts
  and all variable updates within the same frame
- Physics at 60Hz ticking 6 times this frame: events are visible on all
  6 ticks (not consumed on the first)
- AI at 10Hz ticking once: sees the same events physics saw
- Variable updates: still see the events (useful for debug display, etc.)

### sim->tick vs fixed.tick

`sim->tick` counts frames (event cycles). It bumps once per frame regardless
of how many fixed ticks fired.

`fixed.tick` counts accumulator fires for a specific fixed context. Physics
at 60Hz running for 10 seconds: `physics->tick == 600`. This is the counter
for replay indexing, deterministic sequencing, or anything tied to the
fixed rate.

```c
void replay_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Sim_Data* data = user;
    if (data->fixed->tick < data->recording->count) {
        Move_Event* move = &data->recording->recorded[data->fixed->tick];
        mel_sim_push(sim, EVT_MOVE, move, sizeof(*move));
    }
}
```

### mel_sim_clear (Manual Path)

`mel_sim_clear` still exists for manual-path users who drive the loop
themselves and don't use `mel_sim_register`. When the engine drives sims,
it calls `mel_sim_clear` automatically — the game never needs to.

---

## User Data

Two levels:

**Sim-level** (`sim->user`): scene-wide context. Set during `mel_sim_init`.
Shared by all updates on this sim.

**Per-update** (`.user` at registration): callback-specific data. Each
update can have its own context.

```c
mel_sim_init(&sim, .user = &game_state, ...);

mel_sim_fixed_add_update(physics, physics_tick, .user = &physics_world);
mel_sim_fixed_add_update(physics, collision_tick, .user = &collision_data);
mel_sim_add_variable(&sim, push_sprites, .user = &render_data);
```

Inside a callback:
```c
void physics_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Game_State* game = sim->user;       // scene-level
    Physics_World* phys = user;          // callback-level
}
```

---

## Scene Lifecycle

The simulation is the organizational unit for scenes. Starting a scene =
create a sim, wire it up, register. Stopping = unregister, tear down.

```c
void start_gameplay(void) {
    mel_sim_init(&gameplay_sim, .seed = 42, .user = &game,
        .event_buffer = buf, .event_buffer_size = sizeof(buf));
    gameplay_world = mel_world_create();

    Mel_Sim_Fixed* phys = mel_sim_add_fixed(&gameplay_sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(phys, gameplay_tick, .user = &gameplay_world);
    mel_sim_add_variable(&gameplay_sim, gameplay_present);

    mel_sim_register(&gameplay_sim);
}

void stop_gameplay(void) {
    mel_sim_unregister(&gameplay_sim);
    mel_world_destroy(gameplay_world);
}
```

Scene transitions: unregister old sim, register new sim. Pause menu:
separate sim (gameplay paused via `time_scale = 0` or unregistered).
