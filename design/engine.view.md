# Rendering — Views

## Status

This document describes the target view architecture (vNext).

It fills the gap between:
- `engine.source.md` — typed render inputs attached to views
- `engine.render.md` — render lists as camera-agnostic world data
- `engine.frame.recipe.md` — frame-level planning and technique participation
- `engine.technique.md` — execution strategies chosen for those views
- `engine.render.graph.md` — passes and targets as execution topology
- `engine.app.md` / `engine.frame.md` — init-time wiring, engine-driven frame execution

Current implementation: none. This is a design target.

---

## Why Views Exist

The engine already has:
- **Render lists**: what data exists to be drawn
- **Cameras**: how world-space data is seen
- **Render targets**: where pixels are written
- **Passes**: how work is executed
- **Windows**: swapchain leaves in the graph

But these pieces alone do not describe a very common engine-level concept:

> "Render this part of the world, with this camera and policy, into this
> region/output, and compose it with the rest of the frame."

That concept is a **view**.

A view is the missing orchestration layer between:
- app/game intent
- engine-provided default rendering
- low-level graph execution

Without views, simple rendering feels too low-level:
- the app has to create passes just to get a world view on screen
- minimaps, HUD overlays, split-screen, editor viewports, and offscreen captures
  all collapse into ad hoc graph wiring
- the engine cannot provide strong defaults without baking in one fixed pipeline

With views, the engine can own the boring orchestration while still letting the
app drop down to passes and raw graph topology whenever it wants.

---

## Core Idea

A **view** is a persistent render configuration object.

It is:
- not a camera
- not a render pass
- not a render target
- not a window

It is the thing that says:
- which data sources are visible
- how they are seen
- where their result goes
- how that result is composed

In short:

- **Render list** = what to draw
- **Camera** = how to see world-space data
- **View** = where/how a scene is rendered and composed
- **Pass** = implementation node that does the actual work
- **Window** = one possible final presentation destination

Views are engine-facing objects. The engine may synthesize one or more passes
from them in the render graph, but the view itself is a higher-level concept
than a pass.

In the full architecture, views are normally consumed by a frame recipe rather
than compiled straight into graph nodes in isolation.

---

## Design Goals

1. **Default rendering should be easy.**
   The app should be able to create a window, create one or more views, attach
   sprite/mesh/text lists, and let the engine handle the default pass wiring.

2. **The graph remains the real execution model.**
   Views do not replace the render graph. They expand into graph topology.

3. **Views must support composition, not just fullscreen rendering.**
   A view can be:
   - the main game camera
   - a HUD overlay
   - a minimap
   - a split-screen pane
   - an editor viewport
   - an offscreen capture for later sampling

4. **The engine must not force one "game pipeline".**
   Views are reusable graph-native building blocks, not a canned fixed-resolution
   stack that every project must accept.

5. **The app can take over any step.**
   Users can:
   - use the engine's default view execution
   - extend it with custom passes
   - bypass it entirely and wire the graph manually

---

## What A View Owns

A view owns configuration, not scene data.

```c
typedef struct Mel_View Mel_View;

typedef struct {
    str8 name;

    const Mel_Camera* camera;     // nullable; overlay/composite views may not need one

    Mel_Render_Target* target;    // nullable; if null, target is resolved from parent/window binding

    Mel_Rect viewport;            // destination rectangle in the target
    bool viewport_is_normalized;  // false = pixels, true = 0..1 relative rect

    bool clear_color_enabled;
    Mel_Vec4 clear_color;

    bool clear_depth_enabled;
    f32 clear_depth;

    u32 composition_mode;         // e.g. opaque replace, alpha overlay, additive, custom
    u32 scaling_mode;             // e.g. stretch, fit, fill, pixel-perfect, custom

    void* user;
} Mel_View_Desc;
```

The exact fields are negotiable, but conceptually a view owns:
- an optional camera
- an output target binding
- a viewport rectangle
- clear policy
- composition policy
- scaling policy
- view-local user data

What a view does **not** own:
- render list storage
- ECS worlds
- simulations
- windows
- pipelines
- shaders

Those are separate systems that the app wires to the view.

---

## Sources

A view consumes renderable sources.

At minimum, built-in sources are render lists:

```c
void mel_view_attach_sprite_list(Mel_View* view, Mel_Render_List* list);
void mel_view_attach_mesh_list(Mel_View* view, Mel_Render_List* list);
void mel_view_attach_text_list(Mel_View* view, Mel_Render_List* list);
void mel_view_attach_debug_list(Mel_View* view, Mel_Render_List* list);
```

These are convenience bindings for the engine's default renderers.

More general form:

```c
void mel_view_attach_source(Mel_View* view, Mel_Source_Handle source);
```

This keeps the design open:
- built-in renderers can be attached ergonomically through render-list helpers
- advanced users can attach GPU buffers, targets, or other source kinds

The important point is that the view references sources; it does not own them.

→ Source model and source kinds: `engine.source.md`

---

## A View Is Not A Camera

This distinction is critical.

A camera answers:
- from where do we look?
- with what projection?

A view answers:
- what sources are visible?
- what camera do they use?
- into which target/region do they render?
- how is the result composed?

Examples:

1. **Main world view**
   - camera: gameplay camera
   - sources: sprite list, mesh list, debug list
   - target: swapchain or intermediate hdr target
   - viewport: fullscreen

2. **HUD overlay**
   - camera: null or orthographic UI camera
   - sources: text list, HUD sprite list
   - target: same final target as world view
   - viewport: fullscreen
   - composition: alpha overlay

3. **Minimap**
   - camera: top-down camera
   - sources: same world sprite/mesh lists as main view
   - target: intermediate target or final target
   - viewport: corner rect

4. **Editor viewport**
   - camera: editor camera
   - sources: game world + editor overlays
   - target: sub-rect inside editor window or offscreen target used by UI

Same world data. Different cameras. Different views.

---

## A View Is Not A Pass

Passes are execution units.

Views are orchestration objects that may generate passes.

For example, a simple world view might expand into:
- one graphics pass that renders sprites
- one graphics pass that renders meshes
- one graphics pass that renders text
- one composite pass that writes to its destination

Or it might expand into:
- a single combined pass

Or:
- a deferred stack with depth prepass, gbuffer, lighting, post-process

The app should not have to care unless it wants to.

This is the main reason views are useful:
they let the engine provide defaults without pretending that one pass = one
semantic view.

---

## A View Is Not A Window

Windows are presentation endpoints.

Views can render:
- directly to a window's swapchain target
- to an intermediate render target
- to another view's target
- to an offscreen texture for later sampling

A window may show:
- one fullscreen view
- many views composed together
- no view at all (manual raw graph path)

This keeps multi-window and editor-style composition natural.

---

## View Composition

Views should compose explicitly.

There are two broad categories:

### Scene Views

Views that render world or UI data from sources into a target.

Examples:
- main gameplay camera
- minimap
- editor scene viewport
- offscreen reflection capture

### Composite Views

Views that take one or more already-rendered inputs and place/blend them into
another target.

Examples:
- present world result to a window
- compose main game + HUD + debug overlay
- editor window containing multiple scene panes
- picture-in-picture spectator view

This split is useful because not every view should imply a scene renderer.
Some views exist only to place already-rendered results.

---

## Default Engine Behavior

The engine should provide **default view execution**, not a monolithic default
"game graph".

Meaning:

If the app creates a view and attaches built-in sources, the engine can
automatically synthesize the needed passes.

Example:

```c
static Mel_Window_Handle game_window;
static Mel_View* world_view;
static Mel_View* hud_view;
static Mel_Render_List world_sprites;
static Mel_Render_List hud_sprites;

void app_init(void)
{
    game_window = mel_window_create(S8("Game"), 1280, 720);

    world_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("world"),
        .camera = &game_camera,
        .viewport = mel_rect(0, 0, 1, 1),
        .viewport_is_normalized = true,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.05f, 0.05f, 0.08f, 1.0f),
    });

    hud_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("hud"),
        .viewport = mel_rect(0, 0, 1, 1),
        .viewport_is_normalized = true,
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
    });

    mel_view_attach_sprite_list(world_view, &world_sprites);
    mel_view_attach_sprite_list(hud_view, &hud_sprites);

    mel_window_present_view(game_window, world_view);
    mel_window_overlay_view(game_window, hud_view);
}
```

No manual pass construction required for the common path.

Internally, the engine expands those view registrations into graph topology.

---

## Manual Control

Views are not mandatory.

Three usage modes should exist:

### 1. Default View Mode

The app creates views, attaches sources, and lets the engine generate the pass
topology.

Best for:
- normal 2D/3D game rendering
- tools that want scene views without graph boilerplate
- editor panes

### 2. Extended View Mode

The app uses engine-generated views but injects custom passes before, after, or
between them.

Examples:
- custom post-process on a specific view
- GPU particle pass feeding one view
- shadow map generation reused by multiple views

### 3. Raw Graph Mode

The app ignores views entirely and builds the graph manually.

Best for:
- experimental renderers
- very custom frame pipelines
- applications that want full explicit control

Views should be a convenience and composition model, not a prison.

---

## Relationship To The Existing Architecture

Views do not replace the "three independent rendering pieces" model.
They sit above it.

The full stack becomes:

1. **Production**
   ECS sync systems, producers, manual submission

2. **Interchange**
   Render lists and render targets

3. **View Orchestration**
   Views describe how sources become visible outputs

4. **Consumption**
   Render graph executes the pass topology derived from views and custom passes

This preserves the existing architecture while filling the practical gap
between "lists exist" and "pixels appear where I want them".

---

## Suggested API Shape

Persistent object, handle or pointer-based. Exact ownership is open.

Possible API:

```c
typedef struct Mel_View_Handle { u32 id; } Mel_View_Handle;

Mel_View_Handle mel_view_create(const Mel_View_Desc* desc);
void mel_view_destroy(Mel_View_Handle view);

void mel_view_attach_source(Mel_View_Handle view, Mel_Source_Handle source);
void mel_view_detach_source(Mel_View_Handle view, Mel_Source_Handle source);

void mel_view_set_camera(Mel_View_Handle view, const Mel_Camera* camera);
void mel_view_set_target(Mel_View_Handle view, Mel_Render_Target* target);
void mel_view_set_viewport(Mel_View_Handle view, Mel_Rect rect, bool normalized);

void mel_window_present_view(Mel_Window_Handle window, Mel_View_Handle view);
void mel_window_overlay_view(Mel_Window_Handle window, Mel_View_Handle view);
```

Important constraint:
the app creates and wires views at init time, just like simulations and
render-list producers. The engine drives them after that.

---

## Internal Expansion Model

Internally, the engine should compile view registrations through the frame
recipe layer into graph nodes.

For example:

```text
world_view
  reads: world sprite list, world mesh list
  camera: gameplay camera
  output: world_color target

hud_view
  reads: hud sprite list, text list
  camera: null / ui camera
  output: game window swapchain
  composition: alpha overlay over world_color
```

Possible generated topology:

```text
world_sprite_pass  -> world_color
world_mesh_pass    -> world_color
hud_pass           -> game_swapchain
present_pass       world_color -> game_swapchain
```

Or a simpler topology if no intermediate target is needed.

The exact generated passes are an implementation detail.
The view contract is what matters.

→ Frame planning above views: `engine.frame.recipe.md`

---

## Resolution Independence

Views are the correct place to express resolution/scaling policy.

Not every project wants a fixed-resolution game view.
So this must not be a global engine assumption.

Instead, each view can choose its own policy:
- stretch
- fit/letterbox
- fill/crop
- pixel-perfect integer scale
- render at target resolution
- render at custom scale factor

This keeps the engine flexible:
- retro game view can be pixel-perfect
- editor viewport can be fully dynamic
- minimap can render at lower resolution
- AAA 3D view can support dynamic resolution scaling

---

## ImGui

ImGui should not be a special hardcoded "extra pass" bolted onto app code.

Under the view model, imgui becomes one of:
- a built-in overlay source attached to a view
- a built-in overlay view attached to a window

This removes one of the biggest current pain points in demo code:
the app should not need to manually create an imgui pass just to get debug UI.

---

## Multi-Window

Views make multi-window composition natural.

Examples:
- one gameplay view presented to two windows with different cameras
- one world rendered to both a game window and an editor window
- one editor window containing multiple sub-views
- detached inspector window showing a separate preview view

Since the graph is already global in the design, windows remain leaves and
views simply route work toward those leaves.

---

## Open Questions

These need interface negotiation before implementation:

1. **Ownership model**
   Should views be raw pointers, generational handles, or window-owned objects?

2. **How much of the default pipeline is view-driven?**
   Do built-in sprite/mesh/text renderers register themselves globally, or does
   each view explicitly name which renderer stages it wants?

3. **Composite views vs scene views**
   Should these be one type with flags, or two separate object kinds?

4. **Window integration**
   Should windows merely expose swapchain targets, or also maintain an ordered
   root-view list for default presentation?

5. **World integration**
   Does `mel_world_present_to(world, window)` become:
   - `mel_world_attach_to_view(world, view)`?
   - `mel_view_attach_world(view, world)`?
   - or stay as a convenience wrapper over view creation?

6. **Sampling view outputs**
   Should a view expose its resolved output target so custom passes can consume
   it directly?

---

## Summary

Views are the missing bridge between:
- render data
- cameras
- windows
- graph execution

They let the engine provide strong defaults without forcing a canned pipeline,
and they let apps stay declarative until they genuinely need low-level control.

That makes them the right abstraction for the engine's rendering entrypoint:
high enough to remove boilerplate, low enough to preserve explicit control.
