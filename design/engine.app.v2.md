# Application Layer

Simulation, input, canvas, and stage. Everything above the render graph that the engine provides to the application.

Render composition is covered in render.composition.md. This document covers everything else the engine offers.

## Simulation

Owns game state and drives fixed/variable updates. Pure logic, no rendering, no input knowledge.

### Mel_Sim

Handle-based. The sim is a timeline coordinator, not a god object.

```c
Mel_Sim sim = mel_sim();
mel_sim_set_state(sim, state_ptr);
mel_sim_add_fixed(sim, fn, rate, user);
mel_sim_add_variable(sim, fn, user);
mel_sim_start(sim);
```

### Callback signature

Both fixed and variable callbacks receive the sim handle and a user pointer:

```c
void (*)(Mel_Sim sim, void* user)
```

The sim handle gives access to:
- `mel_sim_get_state(sim)` — the sim's state pointer
- `mel_sim_dt(sim)` — this callback's dt (fixed dt for fixed, frame dt for variable)
- `mel_sim_alpha(sim)` — interpolation alpha (for variable callbacks reading fixed state)
- `mel_sim_frame_dt(sim)` — raw frame delta time
- `mel_sim_tick(sim)` — current tick count

### Fixed vs Variable

Fixed updates run at a fixed rate (e.g. 8hz for snake, 60hz for physics). The sim accumulates frame time and fires fixed updates as needed. Multiple fixed contexts can coexist on one sim at different rates.

Variable updates run once per frame. Used for any per-frame logic that isn't rendering (camera smoothing, interpolation, input-driven state changes).

### State

The sim holds a state pointer via `mel_sim_set_state`. It does not own or allocate the state — that's the caller's responsibility. The state is accessible from any callback via `mel_sim_get_state(sim)`.

The `user` pointer on each callback is per-callback context, independent of sim state. For example, a draw callback receives the canvas handle as `user`, not as part of game state.

### Sim does not know about

- Rendering (no draw callbacks on the sim itself)
- Input (no event processing)
- Composition (no routes or targets)

### Redesign note

This is a proposed redesign of the current simulation system. The existing implementation uses `Mel_Sim_Ctx`, `Mel_Sim_Fixed`, `mel_sim_add_fixed/remove_fixed`, `mel_sim_fixed_add_update`, `mel_sim_add_variable/remove_variable`, and `mel_sim_tick`. The new API consolidates these into a single handle-based interface. Migration path TBD.

---

## Input

Tree-based dispatch. Device-agnostic action bindings. State-based queries.

### Input Handle

```c
Mel_Input in = mel_input_create();
mel_input_bind(in, MEL_KEY_UP, ACT_UP);
mel_input_bind(in, MEL_KEY_ESCAPE, ACT_QUIT);
```

`Mel_Input` is a handle. It holds bindings (key/button -> action) and tracks action state (pressed, held, released, value).

`MEL_KEY_*` are engine-wrapped keycodes. No SDL types leak outside the engine.

Future: `mel_input_bind(in, MEL_PAD_DPAD_UP, ACT_UP)` for gamepad, `mel_input_bind(in, MEL_COMBO(MEL_KEY_CTRL, MEL_KEY_R), ACT_RESTART)` for key combinations.

### Action Queries

State-based, not event-based. Queried at any time (typically inside sim tick).

```c
mel_action_pressed(in, ACT_UP)      // edge: triggered since last tick. true for one tick.
mel_action_held(in, ACT_UP)         // level: currently active. true every tick while held.
mel_action_released(in, ACT_UP)     // edge: released since last tick.
mel_action_value(in, ACT_UP)        // analog: 0.0-1.0 for triggers, -1.0 to 1.0 for sticks.
```

The engine maintains action state from OS events (SDL key_down/key_up internally). The app never sees raw events for bound actions.

### Input Tree

Inputs arrive globally from the OS. The engine dispatches them through a tree of input handles.

Dispatch order: leaves first. Siblings cannot block each other. If no leaf at a level captures the input, it bubbles up to the parent.

```c
Mel_Input_Layer layer = mel_input_layer_create();
mel_input_layer_attach(layer, game_input);
mel_input_layer_attach(layer, hud_input);    // sibling of game_input, can't block it
```

Pushing a new layer on top (e.g. menu): children of the new layer get priority. If they capture the input, it doesn't reach the game layer. If they don't, it falls through.

This handles:
- Menu over game: menu layer captures menu keys, game still gets movement if menu doesn't eat it
- 1v1 fighter: P1 and P2 input handles as siblings, can't block each other
- Text input field: push a layer that captures raw key events

### Raw Events

For cases where action bindings aren't enough (text input, mouse tracking, drag-and-drop), the app can register event handlers on the input tree that receive `Mel_Event*` (engine-wrapped, no SDL).

---

## Canvas

2D immediate-mode command buffer. Records draw commands, copy-on-commit semantics.

### API

```c
Mel_Canvas cv = mel_canvas();

mel_canvas_rect(cv, x, y, w, h, color);
mel_canvas_line(cv, x1, y1, x2, y2, color);
mel_canvas_printf(cv, x, y, color, fmt, ...);

mel_canvas_commit(cv);
```

`Mel_Canvas` is a handle.

### Commit Semantics

- Recording goes into the back buffer.
- `mel_canvas_commit(cv)` snapshots: copies the back buffer to the front, clears the back for new recording.
- If you never commit again, the last committed content keeps displaying.
- No explicit clear needed — the snapshot clears the recording buffer.

### Usage Patterns

Static content (grid): record once, commit once, never touch again.
Dynamic content (game sprites): record each frame, commit at the end.

### Relation to Render Graph

A canvas is a way to populate a GPU resource. The render graph doesn't know about canvases. A canvas produces a buffer of draw commands. Something (a pass, or pre-graph code) uploads that buffer to the GPU. A render pass consumes the GPU buffer.

Canvas is convenience for simple 2D. It is not a core rendering concept.

---

## Stage

Bundles lifecycle for a game mode. Handles init (and optionally shutdown) of sim, input, and state.

### API

```c
Mel_Stage stage = mel_stage(.init = my_init, .user = context);
mel_stage_add(stage);
```

The stage init function receives `(Mel_Stage stage, void* user)` and is responsible for:
- Allocating game state
- Creating canvases
- Setting up input handles and layers
- Creating and starting sims
- Registering passes on the render graph

### Use Cases

**Gameplay stage:** Owns the simulation, routes OS input to the sim, registers render passes that draw the game world.

**Replay stage:** Loads multiple recorded simulations, plays them back with recorded inputs, registers render passes that draw all attempts overlaid (Super Meat Boy style). Same simulation code, completely different input source and rendering.

**Loading stage:** Checks each frame if assets are loaded. When done, creates the target stage and removes itself. The engine could provide a default loading stage.

### What a stage is not

A stage does not own or manage the systems it creates. It's an initialization scope — a place to wire things together. The engine manages the lifecycle of handles (sim, canvas, input) independently.

### Open questions

- Does removing a stage auto-destroy everything created during its init? If so, the stage DOES own things, and the "doesn't own" claim is wrong. Need to pick one.
- Can stages nest (sub-stages)?
- How do stages interact with the render graph? Does each stage register its own passes, or does the app manage one global graph?

---

## Application Lifecycle

```c
void app_init(void)
{
    Mel_Window win = mel_window(.title = S8("Snake"), .width = 640, .height = 480);
    Mel_Stage stage = mel_stage(.init = snake_init, .user = win);
    mel_stage_add(stage);
    mel_window_show(win);
}
```

The engine owns init and shutdown. `app_init` is the entry point. The window is created hidden and shown after setup. Stages handle the rest.

### Five Concerns

1. **Input** — OS events -> input tree -> action state. Engine-owned dispatch, app-owned bindings.
2. **Simulation** — fixed/variable updates mutate game state. Pure logic.
3. **Presentation** — reads state, populates GPU resources (via sync systems, canvases, or custom code). Bridge between sim and rendering.
4. **Render Graph** — passes + resources. Technique-agnostic GPU scheduling. See render.composition.md.
5. **GPU Work** — actual shader execution. Entirely engine-owned. App never touches this unless writing custom passes.

Simulation, input, and the render graph are fully decoupled. They share data through pointers and handles, not through callback coupling.
