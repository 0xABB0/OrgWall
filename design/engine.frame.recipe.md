# Rendering — Frame Recipe

## Status

This document describes the target frame-recipe architecture (vNext).

It fills the gap between:
- `engine.source.md` — typed render inputs available to the planner
- `engine.view.md` — views as visibility and composition intent
- `engine.technique.md` — techniques as execution strategies
- `engine.render.graph.md` — passes, targets, and execution topology
- `engine.app.md` / `engine.frame.md` — init-time wiring and engine-driven frames

Current implementation: none. This is a design target.

---

## Why A Frame Recipe Exists

The engine already has, or is growing toward:
- **render sources** — lists and other typed render data
- **views** — what is visible, where, and how it composes
- **techniques** — engine-native rendering strategies
- **swapchains** — presentation endpoints, including window-backed and image-backed outputs
- **render graph** — the actual pass/resource DAG that executes on the GPU

But something still has to answer:

> "Given these sources, these views, these techniques, and these outputs,
> what frame should exist, and how does it become executable?"

That thing is the **frame recipe**.

Without a frame recipe, the engine still leaks too much execution structure to
the app:
- views exist, but who decides which techniques participate in them?
- techniques exist, but who decides where their outputs go?
- swapchains exist, but who decides what gets composed into them?
- the render graph exists, but who decides when topology should be rebuilt?

The frame recipe is the planning layer above the render graph.

It lets the app describe frame intent declaratively while keeping the graph as
the true execution model underneath.

---

## Core Idea

A **frame recipe** is a persistent description of a frame.

It is:
- not a pass
- not a render graph
- not a window
- not a view
- not a renderer

It is the thing that says:
- which views exist in this frame
- which techniques participate in those views
- which swapchains receive which outputs
- how those outputs are composed
- which parts are default engine behavior vs explicit overrides

In short:

- **Source** = renderable data
- **View** = visibility and composition intent
- **Technique** = rendering strategy
- **Swapchain** = presentation endpoint
- **Frame recipe** = authored frame plan
- **Execution graph** = compiled GPU work

The engine compiles the frame recipe into the render graph.

---

## Design Goals

1. **Default rendering should be declarative.**
   The app should describe views, techniques, and outputs in init, then let the
   engine drive frame execution.

2. **The graph remains the truth.**
   The frame recipe never replaces the render graph. It expands into it.

3. **Modern rendering must fit naturally.**
   Mesh shaders, GPU-driven culling, deferred paths, visibility buffers, and
   future techniques must slot into the same architecture as sprites and text.

4. **Techniques should be first-class engine citizens.**
   Built-in sprite, text, mesh, mesh-shader, deferred, debug, and postprocess
   paths should all be expressible through the same planning layer.

5. **Top-level app code should express intent, not pass wiring.**
   The engine should own the boring orchestration: target setup, pass ordering,
   barriers, presentation, and default composition.

6. **Every layer remains overridable.**
   The app can accept the defaults, extend them, or bypass them.

---

## The Rendering Stack

The rendering architecture becomes:

1. **Sources**
   Sprite lists, mesh lists, text lists, GPU-written lists, procedural sources.

2. **Views**
   What sources are visible, how they are seen, and how results are composed.

3. **Techniques**
   Rendering strategies that know how to consume sources for a view.

4. **Frame Recipe**
   The authored plan that binds views, techniques, and swapchains into a frame.

5. **Execution Graph**
   The compiled pass/resource DAG that actually runs.

This is the missing bridge between:
- "I have data and cameras"
- and
- "the GPU is executing the right work this frame"

---

## What The App Declares

The application should mostly declare a recipe during init.

Conceptually:

```c
typedef struct Mel_Frame_Recipe Mel_Frame_Recipe;

Mel_Frame_Recipe* mel_frame_recipe_create(str8 name);
void mel_frame_recipe_destroy(Mel_Frame_Recipe* recipe);
```

Then wire together persistent objects:
- views
- sources attached to those views
- techniques enabled for those views
- swapchains that receive presented or composed outputs

Example shape:

```c
static Mel_Frame_Recipe* recipe;
static Mel_View_Handle world_view;
static Mel_View_Handle hud_view;
static Mel_Swapchain_Handle game_swapchain;

void app_init(void)
{
    game_swapchain = mel_window_swapchain(game_window);

    world_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("world"),
        .camera = &game_camera,
    });

    hud_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("hud"),
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
    });

    mel_view_attach_sprite_list(world_view, &world_sprites);
    mel_view_attach_mesh_list(world_view, &world_meshes);
    mel_view_attach_sprite_list(hud_view, &hud_sprites);
    mel_view_attach_text_list(hud_view, &hud_text);

    recipe = mel_frame_recipe_create(S8("game"));

    mel_frame_recipe_use_technique(recipe, world_view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_use_technique(recipe, world_view, MEL_TECHNIQUE_MESH);
    mel_frame_recipe_use_technique(recipe, hud_view,   MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_use_technique(recipe, hud_view,   MEL_TECHNIQUE_TEXT);

    mel_frame_recipe_present(recipe, world_view, game_swapchain);
    mel_frame_recipe_overlay(recipe, hud_view, game_swapchain);
}
```

The exact API is negotiable.

The important part is the mental model:
- the app defines persistent frame structure
- the engine compiles and drives it

---

## What The Engine Compiles

The engine should compile a frame recipe into:
- technique participation per view
- intermediate target allocation
- composition topology
- pass ordering constraints
- graph passes and resources
- presentation wiring for swapchains

That compilation has two distinct kinds of work.

### Topology Compilation

Runs when structure changes:
- a view is added or removed
- a technique is enabled or disabled
- a swapchain binding changes
- a custom dependency edge is added
- a feature/capability change requires a different plan

This produces the execution graph topology.

### Frame Parameter Refresh

Runs every frame without rebuilding topology:
- camera matrices changed
- viewport changed
- clear color changed
- technique settings changed but still fit the same topology
- source counts and list contents changed
- dynamic resolution scale changed inside an existing technique path

This keeps the engine fast:
- structure changes are relatively expensive
- normal per-frame updates are cheap

---

## Techniques

Techniques are first-class engine subsystems.

→ Technique model and responsibilities: `engine.technique.md`

Examples:
- `sprite`
- `text`
- `mesh.forward`
- `mesh.deferred`
- `mesh_shader`
- `shadow`
- `postprocess`
- `debug`
- `imgui`

A technique is responsible for knowing:
- which source kinds it can consume
- which view kinds it can operate on
- which intermediate resources it needs
- which passes it contributes to the graph
- which capabilities it requires
- which fallback variants it can select

This is what makes modern rendering fit without corrupting the app-facing API.

The recipe should not care whether a world view is rendered with:
- indexed mesh draws
- compute cull + indirect draws
- task/mesh shaders
- a visibility buffer path

That is a technique decision.

The recipe only says:
- this view wants mesh rendering
- this technique family is enabled here
- this output should end up on this swapchain

---

## Capability Negotiation

The frame recipe is also where renderer capability selection becomes coherent.

The engine should let the app ask for technique families rather than one hard
hardcoded implementation path.

For example:

```c
mel_frame_recipe_use_technique(recipe, world_view, MEL_TECHNIQUE_MESH);
```

The engine can then resolve that to:
1. `mesh_shader` if supported and enabled
2. `mesh.indirect` if supported
3. `mesh.forward` as the basic fallback

This keeps built-in rendering native and modern without making apps brittle.

The recipe should record both:
- the requested technique family
- the resolved concrete implementation for the current device/profile

That resolution should be inspectable in tools and logs.

---

## Swapchains

In Melody, `Mel_Swapchain` should be understood as the presentation-endpoint
abstraction, not only as an OS window backbuffer chain.

Current and intended variants include:
- window-backed swapchains
- image-backed/headless swapchains
- future export or stream-oriented swapchains

This is stronger than inventing a separate `surface` layer prematurely, because
the engine already has a real abstraction here.

Conceptually:

```c
typedef struct { Mel_SlotMap_Handle handle; } Mel_Swapchain_Handle;

Mel_Swapchain_Handle mel_window_swapchain(Mel_Window_Handle window);
```

Then presentation/composition becomes:
- view -> swapchain
- composite view -> swapchain
- offscreen/image-backed recipe output -> image swapchain

That fits:
- multi-window tools
- headless capture workflows
- PNG/video output
- automated rendering tests

---

## Views And Recipes

Views and recipes should stay separate.

A view describes one render/composition intent.
A recipe describes the whole frame.

That distinction matters because:
- the same view may be reused in more than one composition
- multiple views may target the same swapchain
- one view may be rendered by multiple techniques
- one technique may serve many views

So:
- **view** = one node of rendering intent
- **recipe** = the full authored arrangement of those nodes

Views are ingredients.
The recipe is the meal plan.

---

## Recipe Modes

Three usage modes should exist.

### 1. Preset Recipe Mode

The app only declares:
- views
- sources
- technique families
- swapchains

The engine generates the rest.

Best for:
- games that want strong defaults
- tools that want multiple viewports without graph boilerplate
- small teams that do not want to own rendering topology

### 2. Extended Recipe Mode

The app uses the recipe system but injects custom behavior:
- extra passes before or after a technique
- custom target overrides
- extra dependencies between generated nodes
- custom compositors or post-process stages

Best for:
- projects that want engine-native defaults plus targeted control

### 3. Raw Graph Mode

The app bypasses recipes and builds the render graph manually.

Best for:
- experimental renderers
- research paths
- full custom engines built on Melody's lower layers

The frame recipe must be a strong default, not a prison.

---

## Extraction Boundary

The frame recipe should consume extracted render state, not simulation state
directly.

Flow:

1. simulations advance authoritative state
2. extraction updates sources
3. the frame recipe selects views/techniques/swapchains
4. the engine compiles or refreshes the execution graph
5. the graph executes

This keeps the architecture consistent with Melody's broader design:
- deterministic sims stay deterministic
- rendering remains a consumer of extracted state
- tests can drive the same sim path without caring about pass plumbing

---

## Internal Compilation Model

Internally, a frame recipe should compile to something like:

```text
recipe
  -> resolve technique families against capabilities
  -> gather participating views
  -> assign or reuse intermediate targets
  -> build composition tree per swapchain
  -> ask each technique to contribute graph nodes
  -> add presentation nodes for swapchains
  -> compile final execution graph
```

The recipe should not itself become a second graph API.

That would leak execution structure back into the app and defeat the point.

Instead, it should stay focused on:
- intent
- participation
- composition
- default policy
- explicit override points

---

## Relationship To The Existing Architecture

This extends the current rendering design rather than replacing it.

The full stack becomes:

1. **Production**
   ECS sync systems, producers, manual submission, extraction

2. **Interchange**
   Render lists and render targets

3. **Intent**
   Views and frame recipes

4. **Execution Strategy**
   Techniques and their compiled contributions

5. **Consumption**
   Render graph execution

This is the engine-level answer to:
"How do defaults, composition, and modern rendering all coexist without
forcing app code to wire passes by hand?"

---

## Suggested API Shape

Possible API surface:

```c
typedef struct Mel_Frame_Recipe_Handle { u32 id; } Mel_Frame_Recipe_Handle;

Mel_Frame_Recipe_Handle mel_frame_recipe_create(str8 name);
void mel_frame_recipe_destroy(Mel_Frame_Recipe_Handle recipe);

void mel_frame_recipe_use_technique(
    Mel_Frame_Recipe_Handle recipe,
    Mel_View_Handle view,
    Mel_Technique_Family_Id family);

void mel_frame_recipe_disable_technique(
    Mel_Frame_Recipe_Handle recipe,
    Mel_View_Handle view,
    Mel_Technique_Family_Id family);

void mel_frame_recipe_present(
    Mel_Frame_Recipe_Handle recipe,
    Mel_View_Handle view,
    Mel_Swapchain_Handle swapchain);

void mel_frame_recipe_overlay(
    Mel_Frame_Recipe_Handle recipe,
    Mel_View_Handle view,
    Mel_Swapchain_Handle swapchain);

void mel_frame_recipe_add_override(
    Mel_Frame_Recipe_Handle recipe,
    const Mel_Frame_Override* override);
```

This keeps init-time registration aligned with the rest of Melody:
- create persistent objects in init
- register them once
- engine drives them each frame

---

## Modern Rendering Implications

If Melody wants first-class modern rendering, the frame recipe is where that
becomes manageable rather than chaotic.

Examples:

### Simple 2D

- sources: sprite list, text list
- views: world, HUD
- techniques: `sprite`, `text`
- swapchain: game window

Result: one recipe, almost no boilerplate.

### Mid-Range 3D

- sources: mesh list, lights, decals, debug
- views: main camera, minimap, editor scene pane
- techniques: `mesh.forward`, `shadow`, `debug`, `imgui`
- swapchains: game window, editor window

Result: engine handles shared shadow setup and per-swapchain composition.

### High-End GPU-Driven 3D

- sources: meshlets, material tables, GPU cull lists, visibility buffers
- views: gameplay, reflection probe capture, photo mode, editor preview
- techniques: `mesh_shader`, `shadow`, `deferred`, `postprocess`
- swapchains: window-backed HDR output, image-backed capture, tool preview output

Result: the app still declares intent at the recipe level while the engine
expands it into a much richer graph.

That is how the same architecture supports both toy-engine ergonomics and
AAA rendering ambition.

---

## Open Questions

These need interface negotiation before implementation:

1. **Ownership model**
   Should recipes be handle-based objects, module-static slots, or caller-owned
   values compiled into an internal registry?

2. **Technique registration**
   Are techniques globally registered engine modules, or explicitly attached to
   a recipe instance?

3. **Swapchain model**
   Is `Mel_Swapchain` enough as the presentation-endpoint abstraction, or is a
   higher-level wrapper still needed later?

4. **Composition model**
   Should composition be expressed as ordered attachments, explicit composite
   views, or a small composition DSL?

5. **Compilation cadence**
   Do recipes compile lazily on first use, eagerly at init, or on explicit
   `mel_frame_recipe_compile(...)`?

6. **Inspection and tooling**
   How does the app inspect the resolved plan, chosen technique variants, and
   generated graph structure in debug builds?

7. **Raw graph coexistence**
   If a swapchain is partially recipe-driven and partially raw-graph
   driven, what is the cleanest ownership boundary?

---

## Summary

The frame recipe is the missing planning layer between:
- views and sources
- techniques and capabilities
- swapchains and composition
- authored intent and executable graph topology

It gives Melody the right default shape:
- high-level and declarative at the top
- modern and ambitious in the middle
- explicit and uncompromising at the bottom

That makes it the right abstraction for the engine to own if the goal is:
simple when you want simple, powerful when you need powerful, and never hacked
together in between.
