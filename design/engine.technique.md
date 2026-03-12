# Rendering — Techniques

## Status

This document describes the target technique architecture (vNext).

It fills the gap between:
- `engine.source.md` — typed render inputs techniques consume
- `engine.frame.recipe.md` — frame planning and technique participation
- `engine.view.md` — views as visibility and composition intent
- `engine.render.graph.md` — the final execution topology

Current implementation: none. This is a design target.

---

## Why Techniques Exist

The engine needs a first-class way to express:

> "Given these sources and this view, how should rendering actually happen?"

That is the role of a **technique**.

Without techniques as first-class engine concepts:
- views become too smart and start owning renderer behavior
- the frame recipe becomes a vague planner with no execution strategy model
- low-level graph construction leaks back into app code
- modern rendering paths become special cases instead of native engine features

Techniques are the execution-strategy layer of the rendering stack.

They let the engine say:
- sprites are rendered this way
- text is rendered this way
- forward meshes are rendered this way
- deferred lighting is rendered this way
- mesh shaders are used this way
- shadows, postprocess, debug, and imgui all participate coherently

---

## Core Idea

A **technique** is an engine-native rendering strategy module.

It is:
- not a view
- not a frame recipe
- not a pass
- not a source
- not the whole renderer

It is the thing that knows:
- which source kinds it can consume
- which view kinds it can operate on
- which graph nodes it contributes
- which intermediate resources it needs
- which capabilities it requires
- which fallback variants it can resolve to

In short:

- **View** = what should be visible and where
- **Technique** = how a class of rendering work happens
- **Graph** = where that technique's compiled work executes

This is how Melody can support both:
- "draw my sprites without pain"
- and
- "run a mesh-shader visibility-buffer path with async compute"

without inventing two different rendering architectures.

---

## Design Goals

1. **Techniques are first-class engine features.**
   Sprite, text, mesh, shadow, postprocess, debug, imgui, and future modern
   paths should be native modules, not examples or copied boilerplate.

2. **Techniques are not monolithic renderer ownership.**
   A technique contributes part of a frame. It does not own the entire frame.

3. **Techniques must support capability-driven resolution.**
   "Mesh rendering" may resolve to mesh shaders, indirect draws, or forward
   raster depending on hardware and project policy.

4. **Techniques must be composable.**
   Multiple techniques can participate in one view. One technique can serve
   multiple views.

5. **The app should request families, not micromanage passes.**
   The app says "this view wants mesh rendering", not "add these seven passes".

6. **Raw control remains possible.**
   A technique can be configured, overridden, replaced, or bypassed.

---

## Families And Variants

The app should usually request a **technique family**, not a concrete backend.

Examples of families:
- `sprite`
- `text`
- `mesh`
- `shadow`
- `debug`
- `postprocess`
- `imgui`

Examples of concrete variants inside those families:
- `mesh.forward`
- `mesh.deferred`
- `mesh.indirect`
- `mesh_shader.visibility_buffer`
- `mesh_shader.forward`

This distinction matters.

The app's authored intent should be stable:

```c
mel_frame_recipe_use_technique(recipe, gameplay_view, MEL_TECHNIQUE_MESH);
```

The engine then resolves that request against:
- hardware capabilities
- project policy
- view requirements
- attached source kinds
- feature toggles

Possible resolution:

```text
requested family: mesh
resolved variant: mesh_shader.visibility_buffer
fallback chain:
    mesh_shader.visibility_buffer
    mesh.indirect
    mesh.forward
```

This keeps the app-facing API durable while still making modern rendering
first-class.

---

## What A Technique Owns

A technique owns rendering behavior and configuration.

Conceptually:

```c
typedef struct Mel_Technique_Handle { u32 id; } Mel_Technique_Handle;

typedef struct {
    str8 name;                 // "mesh.deferred", "sprite", ...
    str8 family;               // "mesh", "sprite", ...
    u64 source_mask;           // what source kinds are supported
    u64 view_mask;             // what view kinds are supported
    u64 capability_mask;       // required GPU features / engine capabilities
    u32 flags;                 // transparent, overlay, async-compute capable, etc.
} Mel_Technique_Desc;
```

A technique may also own:
- default settings
- variant-resolution policy
- target requirements
- shader/pipeline handles
- debug/introspection metadata

What it should not own:
- the app's views
- the app's sources
- the whole frame recipe
- the whole render graph

Those remain separate layers.

---

## Technique Responsibilities

When participating in a recipe, a technique is responsible for:

1. **Declaring compatibility**
   Which source kinds and view kinds it understands.

2. **Declaring requirements**
   Needed capabilities, formats, depth requirements, async compute usage, and
   other prerequisites.

3. **Resolving to a concrete variant**
   Picking the concrete implementation that will actually run.

4. **Contributing graph structure**
   The passes, intermediate targets, dependencies, and scheduling hints it
   needs for that recipe/view combination.

5. **Publishing outputs**
   Which produced targets or buffers downstream techniques/compositors may
   consume.

6. **Exposing configuration**
   Tunables such as quality modes, sample counts, shadow resolution, cull
   policy, or debug visualizations.

---

## Techniques Are Native Modules

If Melody is going to be batteries-included, techniques should be engine-owned
modules with honest basic implementations.

The built-in set should eventually include:
- `sprite`
- `text`
- `mesh.forward`
- `mesh.deferred`
- `mesh_shader`
- `shadow`
- `debug`
- `imgui`
- `postprocess`

Not all have to ship immediately.
But the architecture should assume they are real engine citizens.

That means:
- discoverable
- configurable
- inspectable
- reusable
- replaceable

not "random pass snippets in examples."

---

## Suggested Registration Model

Techniques should be globally registered engine modules.

Conceptually:

```c
typedef struct Mel_Technique_Family {
    str8 name;
    u32 family_id;
} Mel_Technique_Family;

Mel_Technique_Family mel_technique_family(str8 name);
Mel_Technique_Handle mel_technique_resolve(
    Mel_Technique_Family family,
    const Mel_Technique_Request* request);
```

Built-in families should have stable IDs or constants:

```c
typedef enum {
    MEL_TECHNIQUE_SPRITE,
    MEL_TECHNIQUE_TEXT,
    MEL_TECHNIQUE_MESH,
    MEL_TECHNIQUE_SHADOW,
    MEL_TECHNIQUE_DEBUG,
    MEL_TECHNIQUE_POSTPROCESS,
    MEL_TECHNIQUE_IMGUI,
} Mel_Technique_Family_Id;
```

This is better than raw strings as the long-term contract.

Strings are still useful for:
- tooling
- debug output
- plugins or data-driven configuration

But the engine core should converge toward stable handles/IDs.

---

## The Request Model

The recipe should request families with optional policy.

Conceptually:

```c
typedef struct {
    Mel_Technique_Family_Id family;
    u32 quality;
    u32 preferred_variant;
    u32 fallback_policy;
    u32 flags;
    void* user;
} Mel_Technique_Request;
```

Examples:

```c
mel_frame_recipe_use_technique(recipe, gameplay_view,
    &(Mel_Technique_Request){
        .family = MEL_TECHNIQUE_MESH,
        .quality = MEL_QUALITY_HIGH,
        .fallback_policy = MEL_TECHNIQUE_FALLBACK_ALLOW,
    });

mel_frame_recipe_use_technique(recipe, hud_view,
    &(Mel_Technique_Request){
        .family = MEL_TECHNIQUE_TEXT,
    });
```

This gives the app fine control without forcing it to own pass topology.

---

## Resolution And Fallback

Technique resolution should be explicit and inspectable.

Resolution depends on:
- device capabilities
- enabled engine modules
- attached source kinds
- view properties
- recipe policy
- project-level preferences

Example:

```text
request:
    family = mesh
    quality = high
    fallback = allow

resolution:
    chosen = mesh_shader.visibility_buffer
    alternates = [mesh.indirect, mesh.forward]
    reasons:
        mesh shaders supported
        required source kinds present
        visibility-buffer path enabled
```

Another machine might resolve the same request to `mesh.indirect`.

This is exactly the point:
the app's architecture should not need to fork for that.

---

## Technique Contribution To The Graph

Techniques should contribute graph structure through a constrained interface.

Not:
- "techniques are free to mutate the whole graph however they want"

Instead:
- the recipe/planner asks a technique to contribute work for a specific view
- the technique contributes nodes/resources within that scope
- the planner owns the final composition and swapchain presentation

Conceptually:

```c
typedef struct {
    Mel_Frame_Recipe_Handle recipe;
    Mel_View_Handle view;
    Mel_Swapchain_Handle swapchain;  // optional, if this view is being presented
    const Mel_Technique_Request* request;
    const Mel_Technique_Resolved* resolved;
} Mel_Technique_Build_Ctx;

typedef void (*Mel_Technique_Build_Fn)(Mel_Technique_Build_Ctx* ctx);
```

This keeps the planner in charge while still making techniques powerful.

---

## Common Built-In Technique Shapes

### Sprite

Consumes:
- sprite sources

Typical contribution:
- one graphics pass
- optional batching or atlas preparation internally

### Text

Consumes:
- text/glyph sources

Typical contribution:
- text shaping may happen earlier in extraction
- one or more overlay graphics passes

### Mesh Forward

Consumes:
- mesh instances
- materials
- lights

Typical contribution:
- depth/opaque pass
- transparent pass
- optional shadow inputs

### Mesh Deferred

Consumes:
- mesh instances
- materials
- lights
- decals

Typical contribution:
- depth or gbuffer pass
- lighting pass
- decal pass
- transparent forward pass

### Mesh Shader

Consumes:
- instance sources
- meshlet sources
- material/light sources

Typical contribution:
- optional compute preparation
- task/mesh or mesh-only shader pass
- visibility-buffer or direct shading path
- fallback publication if unsupported

### Shadow

Consumes:
- shadow-casting geometry
- lights/cascade data

Typical contribution:
- shadow-map or shadow-atlas generation
- published target used by mesh techniques

### Postprocess

Consumes:
- a prior color target

Typical contribution:
- tone mapping
- bloom
- exposure
- color grading
- upscaling

### Debug / ImGui

Consumes:
- debug primitives or imgui draw data

Typical contribution:
- overlay passes late in composition

---

## Relationship To Views

Views should not hardcode renderer behavior.

A view says:
- what is visible
- how it is seen
- where it goes

A technique says:
- how a category of rendering work is executed for that view

This separation is important because the same view may use:
- `mesh` + `shadow` + `debug` + `postprocess`

and another view may use:
- `sprite` + `text` + `imgui`

without changing what a view fundamentally is.

---

## Relationship To The Frame Recipe

The frame recipe is the planner.
Techniques are the execution strategies the planner selects and orchestrates.

The recipe decides:
- which views participate
- which technique families are requested
- which swapchains receive outputs
- where custom overrides are applied

The technique decides:
- how its family is concretely executed
- what resources/passes it needs
- what outputs it publishes

This keeps responsibilities clean:
- recipe = authored intent
- technique = execution strategy
- graph = compiled execution

---

## Relationship To The Render Graph

The render graph remains the final substrate.

Techniques do not replace the graph.
They are one of the things that generate it.

The right mental model is:

```text
sources + views + recipe requests
    -> technique resolution
    -> technique contributions
    -> compiled graph
    -> execution
```

That is a better architecture than exposing the graph directly as the default
app-facing model.

---

## Configuration And Overrides

Techniques need engine-native configuration.

Examples:
- sprite batching thresholds
- text atlas/MSDF mode
- shadow cascade count
- shadow resolution
- mesh culling mode
- preferred mesh path
- postprocess quality
- debug overlays

The engine should support configuration at multiple levels:
- global defaults
- per-recipe overrides
- per-view overrides
- debug/runtime toggles

This is how Melody stays batteries-included without becoming rigid.

---

## Inspection

Technique resolution and graph contribution must be inspectable.

At minimum, debug tooling should expose:
- requested family
- chosen concrete variant
- why that variant was chosen
- why fallbacks were skipped
- which passes the technique contributed
- which targets/buffers it created or reused

This matters a lot for a system that wants to be both:
- easy by default
- and serious enough for modern graphics work

If the engine hides too much here, it becomes impossible to reason about.

---

## Replacement And Custom Techniques

Built-in techniques should not be the only ones possible.

The engine should allow:
- disabling a built-in technique family
- replacing a family implementation
- registering custom families or variants
- injecting custom passes around built-in techniques

But custom techniques should still use the same architectural seam:
- declare compatibility
- declare requirements
- resolve variants
- contribute graph structure

That keeps the extension model consistent with the native engine path.

---

## Open Questions

These need interface negotiation before implementation:

1. **Handle vs enum model**
   Do built-in technique families use enums only, or handles backed by a
   registry with enum sugar?

2. **Source-kind typing**
   How are supported source kinds declared: bitmasks, schema handles, or a more
   general source-type registry?

3. **Variant scope**
   Is a variant resolved per recipe, per view, or per swapchain/view pair?

4. **Cross-technique dependencies**
   How should one technique publish outputs that another technique consumes
   without leaking graph plumbing into app code?

5. **Async compute policy**
   Is async usage decided inside a technique, by the planner, or cooperatively?

6. **Plugin story**
   Are custom techniques compiled in, dynamically loaded, or both?

7. **Tooling**
   Where does recipe/technique introspection live: render debugger, logs, cvars,
   or all of the above?

---

## Summary

Techniques are the execution-strategy layer of Melody rendering.

They are the missing piece that lets:
- views stay about visibility and composition
- frame recipes stay about intent and planning
- the graph stay about execution

That is the right architecture if Melody wants to be:
- trivial for 2D sprite games
- comfortable for normal 3D games
- and still a serious home for modern GPU-driven rendering.
