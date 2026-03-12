# Probe — Stage Runtime And Progress

## Purpose

This probe explores how stages should feel in Melody without fighting:

- `Mel_Sim_Ctx` as the execution unit
- render lists, views, and the graph as rendering structure
- input layers as explicit routing
- async IO/jobs/assets as background work

This probe intentionally does **not** model a stage as a frame-callback object
and does **not** bake loading into the stage noun.

The core idea is:

> a stage owns registrations, not frame callbacks

and:

> progress is composable state, not a special stage type

---

## Terminology

### `Mel_Stage`

A live runtime integration unit.

A stage is the glue between:

- simulations
- rendering
- input
- readiness/progress

It owns registrations into those systems, not the systems themselves.

### `Mel_Progress`

A weighted aggregate of asynchronous or deferred work.

It can observe:

- task handles
- custom progress callbacks
- child progress groups

It reports:

- `0.0 -> 1.0` progress
- failure state
- threshold-based readiness

### `loading stage`

Not a core engine noun.

Just a normal stage that watches a `Mel_Progress` object and performs a handoff
when some threshold is reached.

That keeps loading behavior composable instead of forcing every stage to become
a special "load stage".

---

## Core Shape

```c
typedef struct Mel_Stage Mel_Stage;
typedef struct Mel_Loading_Stage Mel_Loading_Stage;

typedef struct {
    Mel_Progress* progress;
    Mel_Stage*    next;
    f32           ready_at;
    bool          attach_next;
    bool          enable_next;
    bool          disable_self;
    bool          detach_self;
} Mel_Loading_Stage_Opt;

void mel_stage_attach(Mel_Stage* stage);
void mel_stage_detach(Mel_Stage* stage);
void mel_stage_enable(Mel_Stage* stage);
void mel_stage_disable(Mel_Stage* stage);

void mel_loading_stage_init_opt(Mel_Loading_Stage* stage, Mel_Loading_Stage_Opt opt);
#define mel_loading_stage_init(stage, ...) \
    mel_loading_stage_init_opt((stage), (Mel_Loading_Stage_Opt){__VA_ARGS__})
```

The important part is not the exact API.

The important part is that:

- a stage is a concrete runtime contributor
- multiple stages can coexist
- readiness lives in `Mel_Progress`
- a loading handoff can be expressed as an ordinary stage

---

## Example 1: Quake-Style Console

### What The Game Wants

- gameplay continues underneath
- the console animates in and out
- console input is captured while open
- the console font must be ready before the stage is useful

### Attach Behavior

```c
void console_stage_begin(Console_Stage* stage)
{
    stage->font = console_font_request();

    stage->input_layer = game_input_push_layer(&stage->input);
    stage->sim_reg = game_sim_register(&stage->sim);
    stage->render_reg = game_render_attach(&stage->render_source);
}
```

### Progress Reporting

```c
static Mel_Progress_Status console_font_progress(void* user)
{
    Console_Stage* stage = user;
    return (Mel_Progress_Status){
        .value = mel_font_progress(stage->font),
        .failed = mel_font_failed(stage->font),
    };
}

void console_stage_init(Console_Stage* stage, const Mel_Alloc* alloc)
{
    mel_progress_init(&stage->progress, alloc);
    mel_progress_add_custom(&stage->progress, console_font_progress, stage, 1.0f);
}
```

The console can attach immediately and choose whether it waits for
`mel_progress_is_ready(&stage->progress, 1.0f)` before becoming interactive.

---

## Example 2: Fight Loading Handoff

### What The Game Wants

- title stage remains active
- a fight should prepare in the background
- once readiness reaches `0.8`, the fight stage becomes active
- streaming can continue after startup

### Setup

```c
static Mel_Progress      fight_progress;
static Mel_Stage*        fight_stage;
static Mel_Loading_Stage fight_loading_stage;

void title_start_fight(void)
{
    fight_stage = mugen_fight_stage_make(&fight_desc);

    mel_progress_init(&fight_progress, mel_alloc_heap());
    mugen_fight_stage_bind_progress(fight_stage, &fight_progress);

    mel_loading_stage_init(&fight_loading_stage,
        .progress = &fight_progress,
        .next = fight_stage,
        .ready_at = 0.8f,
        .attach_next = true,
        .enable_next = true,
        .detach_self = true);

    mel_stage_attach(&fight_loading_stage.stage);
}
```

### Inside The Fight Stage

The fight stage can contribute to `fight_progress` by adding:

- roster load tasks
- stage-definition parse tasks
- SFF decode tasks
- child progress groups for streaming content

It can stay attached-but-not-usable until the loading stage decides to hand off,
or it can attach early and enable features progressively. Both fit the same
model because readiness is externalized.

---

## Example 3: Diegetic HUD Beside Gameplay

Stages do not need to be hierarchical.

```c
mel_stage_attach(gameplay_stage);
mel_stage_attach(cockpit_hud_stage);
mel_stage_attach(debug_overlay_stage);
```

These stages coexist because each one contributes different registrations:

- gameplay stage: world simulation, world rendering, player control
- cockpit HUD stage: HUD sim, HUD rendering, HUD input focus
- debug overlay stage: tooling and diagnostics

No parent-child scene tree is required.

---

## Why `stage` And `progress`

### `stage`

Better than `scene` for this engine direction.

The thing being managed is "what stage the game is in", not a world-local scene
graph object.

### `progress`

Better than `pending` or `stage_load`.

It describes observed readiness without hardcoding one acquisition path into the
stage concept.

That keeps the stage model thin:

- attach
- detach
- contribute registrations
- optionally observe progress

---

## Open Questions

- what exact lifecycle names should stages use: `begin/end`, `attach/detach`, or
  something else?
- should stage coexistence be organized by lanes or stay fully unconstrained?
- what is the exact deferred-mutation boundary for stage attach/detach?
- should the engine ship a default loading stage, or only `Mel_Progress` and let
  apps build their own handoff stages?
