# Probe — Scene Runtime And Transitions

## Purpose

This probe explores how scene transitions should feel in Melody without
fighting:

- `Mel_Sim_Ctx` as the execution unit
- render lists as interchange
- views as presentation intent
- the render graph as the real execution model
- async-first asset loading

This probe intentionally does **not** model a scene as an object with
`update()` / `render()` callbacks.

The core idea here is:

> a scene owns registrations, not frame callbacks

and:

> a transition commits a runtime delta, not a file ticket

---

## Terminology

### `Mel_Scene_Def`

Static declaration of how a scene is built.

It is not a live scene and not a callback-driven frame object.

It knows how to:

- start building a runtime
- describe what resources are needed
- produce a ready-to-commit runtime package

### `Mel_Scene_Load`

An in-flight asynchronous build.

This is the object returned when the app requests a scene transition.

It owns:

- task handles
- load gates / readiness state
- staging allocations
- failure state
- progress

### `Mel_Scene_Runtime`

The concrete active payload of a scene.

This is the thing that gets committed into the engine.

It owns registrations such as:

- simulations
- worlds referenced by those simulations
- view bindings
- render-list producers
- input layers/routes
- scene-owned runtime resources

This is close to what "scene slice" was trying to express, but
`runtime` is much clearer and more in tone with the engine.

### `Mel_Scene_Transition`

The policy for what happens when a load is committed.

Examples:

- replace current gameplay runtime
- overlay a pause-menu runtime
- preload only, commit later

---

## Naming Notes

### `recipe`

Not in tone.

It sounds soft and magical. Melody's vocabulary is more mechanical:
`ctx`, `graph`, `view`, `target`, `pool`, `gate`, `runtime`, `def`, `opt`.

### `slice`

Usable in conversation, but weak as a public API name.

It is not obvious enough on first read. It also does not match a common
engine concept already present in the codebase.

### `runtime`

Best fit here.

It is direct, mechanical, and describes exactly what matters:
the concrete active registrations and owned state.

---

## Core Shape

```c
typedef struct Mel_Scene_Def Mel_Scene_Def;
typedef struct Mel_Scene_Load Mel_Scene_Load;
typedef struct Mel_Scene_Runtime Mel_Scene_Runtime;

typedef struct {
    u32 mode; // REPLACE / OVERLAY / PRELOAD_ONLY
} Mel_Scene_Transition_Opt;

Mel_Scene_Load* mel_scene_begin(const Mel_Scene_Def* def, void* params);

f32  mel_scene_progress(Mel_Scene_Load* load);
bool mel_scene_ready(Mel_Scene_Load* load);
bool mel_scene_failed(Mel_Scene_Load* load);

void mel_scene_commit_opt(Mel_Scene_Load* load, Mel_Scene_Transition_Opt opt);
#define mel_scene_commit(load, ...) \
    mel_scene_commit_opt((load), (Mel_Scene_Transition_Opt){__VA_ARGS__})
```

The important part is not the exact names.

The important part is that:

- loading returns a scene-load object
- readiness is higher-level than a VFS ticket
- commit installs a prepared runtime into the engine

---

## Example 1: Title Screen To Fight

### What The Game Wants

- title screen sim is active
- user presses Start
- fight scene starts loading in the background
- once character/stage/hud assets are fully ready, switch cleanly

### Static State

```c
static Mel_Sim_Ctx title_sim;
static Mel_Scene_Load* pending_fight_load;
static Mel_Scene_Runtime* active_runtime;

static const Mel_Scene_Def mugen_fight_scene;
```

### Request Transition

```c
void title_on_start_pressed(void)
{
    Mugen_Fight_Params params = {
        .p1_def = S8("/chars/kfm/kfm.def"),
        .p2_def = S8("/chars/kfm/kfm.def"),
        .stage_def = S8("/stages/kfm.def"),
    };

    pending_fight_load = mel_scene_begin(&mugen_fight_scene, &params);
}
```

### Poll In An Existing Sim

```c
void title_tick(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    if (!pending_fight_load)
        return;

    f32 progress = mel_scene_progress(pending_fight_load);
    title_set_loading_bar(progress);

    if (mel_scene_ready(pending_fight_load)) {
        mel_scene_commit(pending_fight_load, .mode = MEL_SCENE_TRANSITION_REPLACE);
        pending_fight_load = NULL;
    }
}
```

### What The Scene Definition Internally Builds

The fight scene definition does not expose VFS tickets.

It internally:

1. starts async reads for stage/char/hud assets
2. waits on asset handles and task handles
3. builds `Mugen_Match`
4. prepares the fight views
5. prepares input routing
6. produces a `Mel_Scene_Runtime`

That runtime might contain:

```c
typedef struct {
    Mel_Sim_Ctx match_sim;
    Mugen_Match* match;

    Mel_View_Handle gameplay_view;
    Mel_View_Handle hud_view;

    Mel_Render_List sprite_list;
    Mel_Render_List hud_text;

    Mel_Input_Stack input_stack;
} Mugen_Fight_Runtime;
```

### Commit Semantics

`mel_scene_commit()` should not mutate engine structure immediately.

It should queue a deferred runtime mutation to be applied at the frame boundary:

- after simulation ticking
- before window/view driving

That matches the deferred mutation model already described in
`design/engine.frame.md`.

---

## Example 2: Pause Menu As Overlay Runtime

### What The Game Wants

- gameplay remains loaded
- gameplay simulation pauses via `time_scale = 0`
- pause menu scene adds its own sim and overlay view
- unpause removes only the pause runtime

### Request Pause Runtime

```c
static Mel_Scene_Load* pause_load;

void gameplay_on_pause_pressed(void)
{
    pause_load = mel_scene_begin(&pause_menu_scene, NULL);
}

void gameplay_present(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    if (pause_load && mel_scene_ready(pause_load)) {
        mel_scene_commit(pause_load, .mode = MEL_SCENE_TRANSITION_OVERLAY);
        pause_load = NULL;
        sim->time_scale = 0.0f;
    }
}
```

### Why This Fits Melody

No special scene callback layer is introduced.

The pause runtime simply contributes more registrations:

- pause-menu sim
- pause-menu input layer
- pause-menu HUD/overlay view

And later removes them.

---

## Example 3: Preload Without Commit

Some transitions want the next runtime ready before use.

```c
static Mel_Scene_Load* next_level_load;

void gameplay_prefetch_next_level(void)
{
    Level_Params params = { .level_path = S8("/levels/forest_02.level") };
    next_level_load = mel_scene_begin(&level_scene, &params);
}

void gameplay_on_exit_reached(void)
{
    if (next_level_load && mel_scene_ready(next_level_load))
        mel_scene_commit(next_level_load, .mode = MEL_SCENE_TRANSITION_REPLACE);
}
```

This is important for the "async by default" direction:

- start work early
- commit only when design/gameplay says so

---

## What A Readiness Gate Should Mean

Scene readiness is broader than file I/O completion.

A scene load should be considered ready only when all required work is done:

- VFS reads complete
- parse/decode work complete
- asset handles have reached loaded-or-failed terminal state
- GPU uploads are complete
- runtime registrations are built

That implies a higher-level gate than a raw VFS ticket.

Conceptually:

```c
typedef struct Mel_Ready_Gate Mel_Ready_Gate;

void mel_ready_gate_add_task(Mel_Ready_Gate* gate, Mel_Task_Handle task);
void mel_ready_gate_add_texture(Mel_Ready_Gate* gate, Mel_Texture_Handle tex);
void mel_ready_gate_add_font(Mel_Ready_Gate* gate, Mel_Font_Handle font);
void mel_ready_gate_add_custom(Mel_Ready_Gate* gate, Mel_Ready_Fn fn, void* user);

f32  mel_ready_gate_progress(Mel_Ready_Gate* gate);
bool mel_ready_gate_ready(Mel_Ready_Gate* gate);
bool mel_ready_gate_failed(Mel_Ready_Gate* gate);
```

This generalizes `Mel_Load_Gate` into something scene transitions can use
without becoming asset-manager flavored.

---

## Why This Seems In Theme

This model stays aligned with the current design docs:

- **simulation remains the execution unit**
  `design/engine.sim.md`

- **views remain presentation intent**
  `design/engine.view.md`

- **render graph remains the execution DAG**
  `design/engine.render.graph.md`

- **assets stay per-type and async-first**
  `design/engine.assets.md`

- **scene transitions become deferred structural mutations**
  `design/engine.frame.md`

It adds orchestration, but does not replace the engine's real primitives.

---

## Current Leaning

If this direction survives, the public vocabulary should probably be:

- `Mel_Scene_Def`
- `Mel_Scene_Load`
- `Mel_Scene_Runtime`
- `Mel_Scene_Transition_Opt`

Not:

- `recipe`
- `slice`
- `scene.update`
- `scene.render`

The right mental model is:

> a scene is a way to build and install runtime structure into the engine

not:

> a scene is an object the engine calls every frame
