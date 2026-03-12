# Task Plan — Rendering Expansion

This document is the implementation rollout plan for the rendering architecture
described in:

- `engine.render.md`
- `engine.source.md`
- `engine.view.md`
- `engine.frame.recipe.md`
- `engine.technique.md`
- `engine.render.graph.md`

The goal is not just "more rendering features". The goal is to make Melody's
rendering stack:

- simple
- complete
- fast
- extendable
- configurable
- native to the engine
- usable with close to zero boilerplate in the app/game

The engine must own incidental rendering complexity.
The game/application must own behavior and intent.

## Core Product Goal

For the common path, a game should be able to do this:

1. create or obtain render sources
2. create one or more views
3. describe a frame recipe
4. let the engine compile and execute it

The game should not have to:

- hand-build graph passes for standard rendering
- create pass-local render targets manually
- create present passes manually
- wire imgui manually
- duplicate boilerplate for common 2D or 3D rendering

At the same time, Melody must still allow:

- custom techniques
- custom graph passes
- GPU-driven rendering
- mesh shader pipelines
- custom presentation and composition
- raw graph ownership when needed

## Principles

1. Intent above execution

   The public API should describe what the app wants rendered, not how to build
   the pass DAG.

2. Native defaults, optional control

   Built-in rendering must be first-class engine functionality, not example code
   disguised as an engine API.

3. Layered escape hatches

   Every high-level abstraction must compile down to lower-level explicit
   rendering primitives. Users can stay high-level or take over progressively.

4. No boilerplate in the game

   If a game repeatedly writes the same initialization/render-graph plumbing,
   that is an engine design failure.

5. No fake abstractions

   Every new layer must represent something real:

- source
- view
- technique
- frame recipe
- compiled frame plan
- render graph
- swapchain

6. Modern rendering is not a bolt-on

   Mesh shaders, indirect pipelines, compute-driven culling, and future
   high-end techniques must fit the same architecture without making the simple
   path worse.

## Architecture Layers

### 1. Sources

Sources are persistent typed render inputs or intermediates.

Examples:

- sprite render list
- text run list
- mesh instance database
- meshlet database
- light database
- render target output used as another technique input

Immediate implementation status:

- render-list-backed source wrappers exist

Planned expansion:

- stable source identity and deduping for wrappers
- retained/ECS-backed sources
- GPU-buffer-backed sources
- target-backed sources as technique inputs
- procedural sources

### 2. Views

Views describe visibility and composition intent.

A view is not a pass and not just a camera.

A view contains things like:

- camera/projection
- viewport or composition destination
- clear policy
- composition mode
- attached sources
- optional per-view user data

Immediate implementation status:

- minimal view object exists
- sources can be attached
- clear color/camera metadata exist

Planned expansion:

- composition semantics made real
- multi-viewport layout
- offscreen view outputs
- editor/tooling view use
- view ordering and layering rules

### 3. Techniques

Techniques are engine-native execution strategies.

Examples:

- sprite
- text
- forward mesh
- deferred mesh
- debug
- mesh shader
- postprocess
- ui/imgui

Immediate implementation status:

- technique family identifiers exist
- first slice only compiles sprite-family rendering

Planned expansion:

- technique registry/descriptor system
- capability resolution and fallback selection
- technique-specific settings
- graph contribution contract
- first-class text technique
- first-class debug and imgui techniques

### 4. Frame Recipes

Frame recipes are authored frame intent.

They answer:

- which views participate
- which techniques are requested for each view
- which views present to which swapchains
- how overlays and composition should behave

Immediate implementation status:

- recipe object exists
- recipe can attach view/technique/swapchain relations

Planned expansion:

- keep recipes authored-only
- no generated graph state inside the recipe
- richer output/composition declarations
- recipe diagnostics and validation

### 5. Compiled Frame Plans

Compiled frame plans are the generated execution state of a frame recipe.

They own:

- generated pass names/ids
- generated intermediate targets
- technique resolution results
- graph insertion/removal bookkeeping
- compile diagnostics

This is the bridge between recipe intent and render graph execution.

Immediate implementation target:

- split generated state out of `Mel_Frame_Recipe`

Planned expansion:

- inspectable plan object
- per-view resolved technique variant reporting
- compile caching and dirty tracking
- topology rebuild vs parameter refresh split

### 6. Render Graph

The render graph remains the execution substrate.

It owns:

- real passes
- resource dependencies
- barriers
- execution ordering
- transient resource lifetime
- GPU submission/presentation

It should not be the default app-facing API for standard rendering.

### 7. Swapchains

`Mel_Swapchain` is the presentation endpoint abstraction.

This is broader than a window-only swapchain. Existing examples already show:

- window-backed presentation
- image/headless/video-style output

Planned expansion:

- recipe/plan integration with multiple swapchains
- explicit composition/presentation policy
- swapchain resize handling through the engine

## Rollout Phases

## Phase 0 — Stabilize The First Slice

Status: in progress

Goals:

- stop leaking graph-owned/generated state into authored recipes
- make the current sprite/view/recipe flow correct
- add enough tests to make future expansion safe

Concrete work:

- introduce `render.frame_plan.*`
- move generated pass/target state from recipe to plan
- make graph own copied pass names
- keep recipe as pure authored intent
- migrate the first example and tests

Exit criteria:

- destroying recipes never depends on graph lifetime
- plan rebuild does not accumulate stale passes
- plan can be destroyed cleanly after graph shutdown

## Phase 1 — Better Source And View Contracts

Goals:

- make source identity stable
- make views represent real composition intent rather than just camera + lists

Concrete work:

- cache/dedupe source wrappers for repeated render-list/target wrapping
- introduce explicit view ordering/composition data
- add view rectangles or composition destinations
- make overlay/replacement semantics real

Exit criteria:

- multi-view rendering to the same swapchain is explicit and deterministic
- attached sources are stable, inspectable engine objects

## Phase 2 — Technique System Becomes Real

Goals:

- move from enum-only technique selection to registered technique families
- allow engine-native defaults and future custom techniques

Concrete work:

- technique descriptor/registry
- source contract declaration per technique
- compile hook that emits graph contributions
- capability/fallback selection
- technique diagnostics

Exit criteria:

- sprite and text are separate first-class techniques
- a view can request multiple techniques
- the compiled plan can report which concrete technique variant won

## Phase 3 — No-Boilerplate Game Path

Goals:

- make the common game/app path require almost no render plumbing
- make init-time declaration the norm

Concrete work:

- engine-owned default frame recipe or stage-level recipe helpers
- automatic default swapchain/view binding helpers
- built-in imgui/debug technique participation
- default sprite/text pipeline wiring
- default resize handling through swapchain/view/plan integration

This phase is where the engine must feel like an engine, not a library of
pieces that the app has to glue together every time.

Example target:

```c
mel_stage_use_default_rendering(stage, window);
mel_stage_attach_sprite_list(stage, world_list);
mel_stage_attach_sprite_list(stage, hud_list);
```

The exact API can differ. The important property is that the app should not
need to manually author pass graphs for common rendering.

Exit criteria:

- simple 2D app/example can render with essentially no manual graph setup
- imgui and present wiring can be free when requested
- multi-window still remains possible

## Phase 4 — Multi-View, Tools, And Composition

Goals:

- make the architecture naturally support editor panes, minimaps, split-screen,
  post chains, and offscreen outputs

Concrete work:

- explicit view composition ordering
- multiple views per swapchain
- offscreen-to-onscreen composition
- tool/editor views
- headless swapchain composition

Exit criteria:

- one recipe can drive game view + hud + minimap + imgui on one swapchain
- another recipe can target a headless swapchain for export/testing

## Phase 5 — 3D And Advanced Native Techniques

Goals:

- make the same architecture power richer 3D scenes without API churn

Concrete work:

- mesh instance sources
- material/light sources
- forward mesh technique
- deferred technique
- shadow techniques
- postprocess techniques

Exit criteria:

- a 3D example uses views/recipes/techniques without hand-building the full
  graph
- the engine still allows custom passes

## Phase 6 — Ultra-Modern GPU Paths

Goals:

- keep Melody relevant for modern rendering instead of baking in only classic
  graphics-pass assumptions

Concrete work:

- GPU-buffer-backed sources
- meshlet/cluster databases
- indirect packet sources
- compute-driven culling/prep techniques
- mesh shader technique families
- async compute planning where hardware allows it

Exit criteria:

- a modern GPU-driven renderer still uses the same top-level model:
  source -> view -> recipe -> plan -> graph
- technique fallback can select mesh shader, indirect, or classic variants

## Phase 7 — Inspection, Tooling, And Debugging

Goals:

- make the system understandable when it gets more powerful

Concrete work:

- inspectable compiled plan data
- debug dump of:
  - attached sources
  - views
  - resolved techniques
  - generated targets
  - generated passes
  - presentation endpoints
- editor/debug UI for the compiled frame plan

Exit criteria:

- when rendering fails or falls back, the user can see why

## Phase 8 — Sim, Extraction, And Testing Integration

Goals:

- keep rendering aligned with the sim and testing model

Concrete work:

- formalize extraction as the seam between simulation state and render sources
- allow deterministic headless run-for-N-frames flows
- enable tests that drive sim input and inspect rendered outputs or compiled
  plans
- integrate headless/image swapchains into automated tests

Exit criteria:

- a game can run deterministic simulation + recipe execution in tests without
  special-case runtime glue

## Immediate Implementation Order

This is the recommended short-term order from the current repository state.

1. Add compiled frame plan support and remove generated state from recipes
2. Make render graph own copied pass names
3. Strengthen frame recipe/plan tests
4. Add real ordering/composition semantics for multi-view same-swapchain output
5. Add first-class text technique or formal sprite-backed text extraction path
6. Migrate `street-carlos` and one more simple example to views + recipe + plan
7. Introduce technique registry
8. Introduce no-boilerplate stage/default-rendering path

## Boilerplate Reduction Rules

Any proposed rendering API should be checked against these rules.

1. A simple example should not define its own presentation pass.
2. A simple example should not allocate its own standard render target unless it
   wants a custom target.
3. A simple example should not wire imgui manually.
4. A simple example should not manually clear and rebuild graph passes for
   standard rendering.
5. A simple example should not need custom shutdown choreography just to keep
   rendering resources valid.

If a common sample breaks one of these rules, the engine should absorb that
complexity.

## Risks And Failure Modes

### Risk: the architecture regresses back into "everything is a render list"

Mitigation:

- keep source kinds explicit
- make techniques declare source contracts

### Risk: recipes become thin wrappers over manual graph assembly

Mitigation:

- insist that plans own graph-generation responsibility
- keep built-in techniques native and real

### Risk: defaults become rigid and hostile to advanced rendering

Mitigation:

- preserve raw-graph escape hatches
- compile high-level abstractions into inspectable lower-level structures

### Risk: modern rendering paths force a different top-level API

Mitigation:

- keep views technique-agnostic
- keep source kinds broad enough for GPU-owned data
- keep techniques as the execution strategy layer

## Definition Of Success

The rendering expansion is successful when all of these are true:

- simple games render with almost no boilerplate
- complex games can stay inside engine-native abstractions longer
- advanced users can still take control where needed
- modern GPU rendering fits the same model
- compiled behavior is inspectable and testable
- the game expresses intent and state, not pass-graph plumbing
