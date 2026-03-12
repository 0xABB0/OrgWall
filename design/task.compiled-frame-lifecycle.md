# Task Plan — Compiled Frame Lifecycle

This document defines the next architectural milestone for Melody rendering.

It sits on top of:

- `engine.source.md`
- `engine.view.md`
- `engine.frame.recipe.md`
- `engine.technique.md`
- `engine.render.graph.md`
- `task.rendering-expansion.md`

The goal is to make the rendering stack not only expressive, but cheap and
correct to drive every frame.

Melody already has the right top-level nouns:

- source
- view
- technique
- frame recipe
- compiled frame plan
- render graph
- swapchain

The missing piece is the lifecycle between them.

## Core Goal

The engine must distinguish between:

1. authored frame intent
2. compiled frame topology
3. cheap per-frame parameter refresh
4. extracted render data updates

Without this split, the architecture remains too rebuild-oriented.

That is acceptable for a first slice.
It is not acceptable for the engine's long-term shape.

The target is:

- topology rebuild only when structure changes
- cheap refresh when only per-frame values changed
- source updates independent from topology churn
- a clean extraction seam between simulation and rendering

## Product-Level Outcome

For a normal game frame, Melody should feel like this:

1. simulation runs
2. extraction updates render sources
3. frame plan refreshes dynamic parameters if needed
4. render graph executes

The game should not be responsible for:

- deciding whether rendering needs a full rebuild
- manually reallocating standard intermediate targets on every content change
- manually re-resolving techniques every frame
- mixing gameplay state reads directly into render submission code

## Why This Matters

This milestone is the bridge between:

- today's correct but still rebuild-heavy rendering stack
- tomorrow's retained scenes, deferred paths, GPU-driven sources, mesh shaders,
  tools, and deterministic test flows

If Melody skips this step, later modern rendering support will either:

- force a second architecture for advanced rendering
- or quietly make the current simple path slower and muddier

Neither is acceptable.

## Architectural Target

The engine should expose three distinct layers of frame state.

### 1. Authored Frame Recipe

Persistent intent:

- views
- requested technique families
- swapchain bindings
- composition rules

This is owned by the app or by engine-owned stage helpers.

### 2. Compiled Frame Topology

Persistent generated state:

- resolved technique variants
- generated graph passes
- generated targets
- dependency structure
- presentation wiring

This is owned by `Mel_Frame_Plan`.

### 3. Frame Refresh State

Cheap mutable state that does not require topology rebuild:

- camera matrices
- clear colors
- viewport rectangles
- view scaling/fit results
- technique settings that keep the same pass structure
- per-frame source counts and contents
- dynamic resolution scale inside an already-selected variant

This should be refreshable without recompiling the whole plan.

## Dirty Model

The engine needs explicit dirty classes.

### Topology Dirty

Requires full plan recompile.

Examples:

- a view is added or removed
- a technique family is added or removed from a view
- a swapchain binding changes
- a technique resolves to a different concrete variant
- a source attachment changes in a way that changes compatibility or pass shape
- output format / depth requirement / composition topology changes

### Parameter Dirty

Requires only per-frame refresh.

Examples:

- camera changed
- clear color changed
- viewport changed
- fitted design size changed
- postprocess settings changed without changing pass structure
- shadow distance changed while the same shadow pipeline stays valid

### Source Content Dirty

Does not require plan rebuild or recipe mutation.

Examples:

- a sprite list got new entries
- a retained mesh instance set changed transforms
- a GPU instance buffer was rewritten
- a render target source got new contents from the previous pass chain

### Source Shape Dirty

May require technique or topology re-evaluation.

Examples:

- a source changes schema
- a source changes storage kind
- a source gains or loses required access policy
- a target source changes format/class in a way techniques care about

## Extraction As A First-Class Seam

Rendering should not read gameplay state ad hoc.

The seam should be:

1. authoritative sim state
2. extraction
3. render sources
4. frame refresh / plan execution

Extraction is where the game says:

- which sprites exist this frame
- which text runs exist this frame
- which mesh instances exist this frame
- which GPU databases or retained sources are updated

This is important for:

- determinism
- tests
- replay
- async rendering preparation
- keeping simulation and rendering decoupled

## Source Goal

This milestone must stop treating render-list wrappers as the whole source model.

At minimum, the next source step should support:

- stable source identity for wrapped render lists
- retained sources as a first-class kind
- GPU-buffer-backed sources
- target-as-source participation

Not all need full production implementations immediately.
But the plan lifecycle should be designed around them now.

## Technique Goal

The plan must stop thinking only in family dispatch.

It should reason about:

- requested family
- resolved variant
- capability reason
- fallback chain
- parameter refresh hooks vs topology compile hooks

That is the difference between:

- "mesh"
and
- "mesh resolved to forward today, mesh-shader tomorrow, without changing app
  code"

## Proposed Engine Shape

Conceptually:

```c
typedef enum {
    MEL_FRAME_DIRTY_NONE         = 0,
    MEL_FRAME_DIRTY_TOPOLOGY     = 1 << 0,
    MEL_FRAME_DIRTY_PARAMETERS   = 1 << 1,
    MEL_FRAME_DIRTY_SOURCE_SHAPE = 1 << 2,
} Mel_Frame_Dirty_Flags;

typedef struct {
    Mel_Frame_Dirty_Flags dirty;
} Mel_Frame_Runtime_State;
```

And:

```c
bool mel_frame_plan_compile(...);   // expensive topology build
void mel_frame_plan_refresh(...);   // cheap per-frame refresh
```

The exact API can differ.
The architectural rule should not.

## Success Criteria

This milestone is successful when:

1. the engine can tell whether a frame needs compile or refresh
2. camera/layout/settings changes do not force topology rebuild
3. source contents can change every frame without recipe churn
4. techniques can expose separate compile-time and refresh-time hooks
5. extraction is the named seam between sim state and render sources
6. the design works for both:
   - a simple sprite/text game
   - a GPU-driven mesh or meshlet renderer

## Recommended Implementation Order

1. Stable source identity and source wrapper dedupe
2. Dirty flags on views, recipes, and plans
3. `mel_frame_plan_refresh(...)` as a real API
4. Technique contract split:
   - compile contribution
   - refresh contribution
5. Extraction helpers for stage/default engine paths
6. One retained-source path
7. One GPU-buffer source path
8. One target-as-source path
9. Diagnostics for:
   - why compile happened
   - why refresh happened
   - which technique variant is active

## Example Targets

This milestone should be proven with at least these examples:

### Example A — Simple 2D HUD Game

- sprite world source
- text HUD source
- moving camera
- score changes every frame
- plan refreshes every frame
- topology rebuild only when a view or technique changes

### Example B — Spinning Mesh Scene With Overlay

- mesh world source
- debug/UI overlay source
- resize changes view/layout
- camera orbit refreshes every frame
- no topology rebuild during ordinary animation

### Example C — GPU-Driven Meshlet Scene

- GPU instance source
- GPU meshlet source
- compute cull source
- mesh-technique family resolves to different variants depending on hardware
- same top-level recipe survives variant changes

## Anti-Goals

This milestone is not:

- a request for more stage convenience wrappers
- a request to rewrite the render graph
- a request to finish mesh shaders immediately
- a request to fold simulation and rendering back together

Those may happen later.
This milestone is about making the architecture's frame lifecycle correct.
