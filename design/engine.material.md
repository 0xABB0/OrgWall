# Rendering — Materials

## Status

This document describes the target material architecture (vNext).

It fills the gap between:
- `engine.source.md` — typed render inputs and intermediates
- `engine.view.md` — view-local visibility and composition intent
- `engine.technique.md` — execution strategies that consume geometry and materials
- `engine.frame.recipe.md` — frame planning and capability negotiation
- `task.compiled-frame-lifecycle.md` — compile vs refresh vs source-shape lifetime

Current implementation: ad hoc material IDs and pass-local assumptions.
This document describes the intended first-class subsystem.

---

## Why A Material Subsystem Exists

Melody already has the right high-level rendering layers:
- sources
- views
- techniques
- frame recipes
- compiled frame plans
- render graph

But geometry still needs a durable answer to:

> "What surface or sprite behavior should this thing render with, and how does
> that survive capability changes, technique changes, and platform changes?"

That is the role of a **material subsystem**.

Without first-class materials:
- mesh and sprite entries devolve into pass-specific IDs
- techniques end up owning shader data contracts that should outlive them
- fallback policy gets scattered across passes and games
- modern paths like deferred, visibility-buffer, or mesh-shader rendering have
  no stable engine-level material contract
- game-defined shading models have no clean way to participate in the default
  planner

Materials are the render-side semantic contract between authored content and
technique execution.

---

## Core Idea

A **material** is not just a shader and not just a bag of parameters.

It is the engine object that says:
- which material family this content belongs to
- which static features define its render shape
- which dynamic parameters and resources it exposes
- which techniques may consume it
- how it may fall back when the preferred path is unavailable

In short:

- **Material family** = semantic contract
- **Material template** = authored reusable definition
- **Material instance** = per-use overrides without redefining topology
- **Material backend** = technique-specific implementation of a family
- **Compiled material state** = per-plan resolved GPU-facing form

This keeps authored intent stable while letting the engine resolve concrete
implementations for desktop, mobile, web, VR, forward, deferred, and future
GPU-driven paths.

---

## Design Goals

1. **Materials are first-class engine objects.**
   They are not hidden inside mesh passes, sprite passes, or ad hoc IDs.

2. **Games can extend the system at every level.**
   New families, new parameter schemas, new backends, new fallback ladders, and
   new defaults must all be possible.

3. **Capability-aware resolution is native.**
   The same authored material should resolve coherently across desktop, mobile,
   web, and VR without rewriting game architecture.

4. **Simple 2D and serious 3D share one model.**
   `sprite.unlit`, `ui`, `surface.pbr`, `decal`, and future custom families
   should all live in the same subsystem.

5. **Materials fit the existing source/view/technique/recipe/plan stack.**
   They should not require a parallel renderer architecture.

6. **Compile and refresh stay distinct.**
   Static material shape changes can recompile plans.
   Dynamic parameter updates should refresh cheaply.

---

## The Material Stack

Melody should treat materials as a small stack of related concepts.

### 1. Material Family

A family is the stable semantic contract.

Examples:
- `sprite`
- `ui`
- `surface`
- `decal`
- `sky`
- `line`
- `post`

A family defines:
- parameter schema and resource slots
- allowed static feature keys
- compatible source schemas
- compatible technique families
- default fallback rules
- default engine templates, if any

Families are the part techniques understand.
Techniques should not care whether a game asset came from Blender, JSON, or
code. They should care that it is a `surface` or `sprite` material with a known
contract.

### 2. Material Template

A template is the authored reusable material definition.

It owns:
- family
- family-local profile or class
- static feature set
- default parameter values
- default texture/buffer bindings
- transparency and render-domain policy
- family-specific fallback preferences

Examples:
- `hero_armor`
- `terrain_rock`
- `hud_frame`
- `enemy_flash`

Templates are asset-like objects.
They are shared by many draw records or scene instances.

### 3. Material Instance

A material instance is a lightweight runtime object that references a template
and overrides dynamic values.

Typical instance overrides:
- tint color
- emissive intensity
- roughness scalar
- atlas region selection
- runtime texture swap

Instances should not redefine the material's render shape.
If the override changes static features, resource layout, render domain, or the
required backend class, that should become a different template.

This distinction is critical for the compiled-frame lifecycle:
- template shape changes may require re-resolution or recompile
- instance value changes should usually be refresh-time only

### 4. Material Backend

A backend is a technique-facing implementation of a material family.

Examples:
- `sprite.unlit.forward`
- `surface.standard.forward`
- `surface.standard.deferred.gbuffer`
- `surface.standard.visibility_buffer`
- `surface.mobile.forward`
- `surface.web.forward`

A backend declares:
- which material family it implements
- which technique family or concrete technique variant it serves
- which capabilities it requires
- which family profiles and static features it matches
- how it compiles GPU-facing state
- how it refreshes per-frame bindings or tables

Backends are to materials what concrete variants are to technique families.

### Ownership Boundary: Technique Variant vs Material Backend

This boundary must stay explicit.

- **Technique variant** owns execution strategy
  - pass structure
  - intermediate targets
  - scheduling
  - queue usage
  - pipeline class / render path topology

- **Material backend** owns shading contract within that strategy
  - shader-facing material layout
  - material parameter packing
  - material resource binding contract
  - backend-specific fallback within the chosen technique path

Examples:

- `mesh.forward.mobile` as a technique variant decides:
  - that rendering is forward
  - which passes exist
  - which depth/color targets are used
  - whether transparency is handled in a later pass

- `surface.standard.forward.mobile` as a material backend decides:
  - which `surface.standard` features are legal in that path
  - how parameters/textures are packed and bound
  - which shading code path is used for that material profile

The technique variant answers:

> "How does this class of rendering execute?"

The material backend answers:

> "How does this material family/profile become concrete inside that execution path?"

If this boundary blurs, Melody will drift back toward pass-local material hacks.

### 5. Compiled Material State

At plan-compile time, the engine resolves the active templates and instances
used by a view into a compiled material set for the selected technique path.

That compiled state may include:
- pipeline/program specialization keys
- descriptor layouts or bindless table bindings
- packed parameter blocks
- material table entries
- classification IDs for deferred or visibility-buffer paths
- diagnostics about fallbacks and unsupported features

This state belongs to the compiled plan, not to the authored template.

---

## Material Data Model

The family contract should distinguish between **static shape** and
**dynamic value**.

### Static Shape

Static shape affects compatibility or compiled topology.

Examples:
- opaque vs alpha-blend vs alpha-test
- unlit vs lit vs transmission
- normal-map enabled
- clear-coat enabled
- double-sided
- deferred-compatible vs forward-only
- uses screen-space history

Changing static shape may require:
- a different material backend
- a different pipeline/program specialization
- a different pass bucket
- a different plan compile result

Static shape belongs to the template.

### Dynamic Value

Dynamic values can change without redefining the backend class.

Examples:
- scalar and vector parameters
- matrix parameters
- texture handles
- buffer bindings
- animation frame selection
- tint/emissive multipliers

Dynamic values belong to the template defaults or instance overrides.

### Contract Categories

A family should be able to declare parameters in categories like:
- numeric values
- colors/vectors/matrices
- texture/image slots
- sampler policy
- structured buffer slots
- static feature keys
- render-domain tags

That contract is what makes engine-native and game-defined materials equally
valid to the planner.

---

## Families, Profiles, And Variants

The word "variant" can mean two different things, so Melody should keep these
layers explicit.

### Family

The stable semantic domain.

Examples:
- `surface`
- `sprite`

### Profile

A family-local authored class inside that domain.

Examples:
- `surface.standard`
- `surface.unlit`
- `surface.foliage`
- `sprite.unlit`
- `sprite.lit`
- `ui.panel`

Profiles are still authored intent.
They are not yet concrete GPU implementations.

### Resolved Backend Variant

The concrete implementation chosen for a profile in a specific technique path.

Examples:
- `surface.standard.forward.desktop`
- `surface.standard.forward.mobile`
- `surface.standard.deferred.gbuffer`
- `surface.standard.visibility_buffer`
- `sprite.unlit.bindless`
- `sprite.unlit.atlased_web`

This is what capability resolution selects.

Keeping these levels separate prevents confusion between:
- "what the material means"
- and
- "how the current device and technique will render it"

---

## Built-In Family Direction

Melody should ship with a small but honest built-in family set.

Likely built-ins:
- `sprite`
- `ui`
- `surface`
- `decal`
- `line`
- `post`

Likely default templates:
- missing sprite
- loading sprite
- white unlit sprite
- default UI panel
- default unlit surface
- default standard surface
- debug line material

The important rule is not the exact list.
The important rule is that engine defaults are real material assets/contracts,
not hardcoded pass exceptions.

---

## Material Template Vs Material Instance

This needs to stay sharp.

### Template

Template changes are structural when they alter:
- family/profile
- static feature set
- render domain
- pass bucket expectations
- parameter/resource layout
- fallback allow/forbid rules

Those changes may require:
- backend re-resolution
- pipeline or program rebuild
- plan recompile

### Instance

Instance changes are non-structural when they only alter:
- parameter values
- texture choices that fit the same slot contract
- per-object tint/emission
- runtime scalar overrides

Those changes should normally require only:
- material refresh
- source content update
- descriptor/table upload

### Rule Of Thumb

If a change affects "which backend class should this use?" it is template
shape.

If a change affects only "what values should the already-selected backend read?"
it is instance state.

---

## How Sources Reference Materials

Materials are a first-class subsystem, but sources are still where draw-visible
data lives.

Common cases:

### Simple 2D / Retained 3D

Geometry entries reference material instances directly.

Examples:
- sprite entry stores `material_instance`
- mesh instance entry stores `material_instance`

The engine can then derive the relevant material set for each view and
technique during plan compile or refresh.

### GPU-Driven / Large 3D

Views may also attach explicit material sources.

Examples:
- `MEL_SCHEMA_MATERIAL_TABLE`
- `MEL_SCHEMA_SURFACE_MATERIAL_DB`
- `MEL_SCHEMA_DECAL_MATERIAL_DB`

This is useful when materials are already packed in GPU-owned tables or when a
technique wants direct indexed lookup rather than per-entry handle chasing.

### Important Rule

The source model and material model should complement each other:
- sources own visible records and storage policy
- materials own surface/shading meaning

Materials should not replace sources.
Sources should not collapse material meaning into pass-local IDs.

### Initial Source Integration Contract

The first implementation should lock down one clear contract before expanding.

#### CPU-Fed / Retained Paths

Visible records reference material instances directly.

Examples:
- sprite entry stores `Mel_Material_Instance_Handle material`
- mesh instance entry stores `Mel_Material_Instance_Handle material`

This should be the default path for:
- sprite rendering
- UI rendering
- ordinary retained 3D mesh instances

It keeps the app-facing model simple and maps directly onto the current
render-list and retained-source architecture.

#### GPU-Driven Paths

GPU-owned scene databases should reference material table IDs or attach
explicit material sources.

Examples:
- instance record stores `u32 material_id`
- view attaches `MEL_SCHEMA_MATERIAL_TABLE`
- material source publishes packed parameter/resource tables

This should be the path for:
- draw-stream sources
- meshlet databases
- visibility-buffer pipelines
- late material resolve paths

#### Implementation Rule

Melody should start with:
- direct `material_instance` handles for CPU/retained records
- material-table IDs or material sources for GPU-driven records

That gives the engine one honest path for simple rendering and one honest path
for modern GPU-driven rendering, without forcing one model onto both.

---

## How Techniques Consume Materials

Techniques should consume materials through an explicit family/backend contract,
not by hardcoding one global material format.

### Technique Responsibility

A technique declares:
- which source schemas it understands
- which material families it understands
- which material backend classes it can consume
- which optional features it can honor

Examples:

- `sprite` technique:
  consumes `sprite` or `ui` materials

- `mesh.forward` technique:
  consumes `surface`, `decal`, and maybe `line`

- `mesh.deferred` technique:
  consumes only deferred-compatible `surface` backends and transparent
  forward-only fallbacks

- `mesh_shader.visibility_buffer` technique:
  consumes material tables that can later resolve shading from IDs

### Material Responsibility

A material backend declares:
- which technique family or concrete technique variant it serves
- which capabilities it needs
- what data it publishes to that technique

Examples:
- forward backend publishes direct shading bindings
- deferred backend publishes gbuffer encode contract
- visibility-buffer backend publishes material classification IDs and resolve
  table layout

### Planner Responsibility

The frame plan should resolve techniques first, then resolve material backends
inside that technique context.

Conceptually:

```text
view + attached sources
-> recipe requests mesh
-> technique resolves to mesh.deferred
-> material system resolves active surface templates for mesh.deferred
-> compiled plan stores both the technique result and material backend result
```

That keeps the responsibilities clean:
- recipe chooses families
- technique chooses execution strategy
- material subsystem chooses surface implementation compatible with that
  strategy

---

## Render Domain, Sorting, And Bucketing

Materials are not just shading metadata.
They also participate in render-domain classification.

Examples of material-domain-affecting shape:
- opaque
- alpha-test
- transparent
- decal
- overlay/UI
- distortion/post

These classifications affect:
- sort key strategy
- pass bucket assignment
- whether the material is legal in deferred or must fall back to forward
- whether the material participates in depth prepass, gbuffer, transparent
  resolve, or overlay composition

### Ownership Split

- **Material template/backend** defines domain-relevant shape
  - opaque vs transparent
  - alpha-test
  - double-sided
  - deferred-compatible vs forward-only

- **Technique variant** decides how those domains map to actual passes
  - gbuffer bucket
  - forward opaque bucket
  - transparent forward bucket
  - decal bucket

- **Sources / draw records** still own sortable instance-level facts
  - depth
  - instance transform
  - material instance handle or material table ID

### Practical Rule

Material shape should be able to change bucket assignment, but bucket execution
still belongs to the technique.

That is:
- material says "this is transparent forward-only"
- technique says "transparent forward-only goes through this pass chain"

This keeps material semantics and execution policy aligned without mixing them.

---

## Capability And Fallback Policy

This is the most important part of the subsystem.

Material resolution should depend on the intersection of:
- device capabilities
- engine/platform profile
- recipe/view policy
- resolved technique variant
- material family fallback rules
- template-level allow/forbid choices

### Capability Classes

The engine should reason in capability classes, not raw API trivia.

Examples:
- bindless available
- multiple render targets available
- storage-buffer-heavy path allowed
- task/mesh shaders available
- subgroup features available
- multiview available
- screen-space history available
- portable web-safe path required

This keeps material resolution stable across backend APIs.

### Policy Layers

Policy should compose from several scopes:
- engine defaults
- project/platform profile
- recipe overrides
- view overrides
- material template overrides

This should align with Melody's technique-family policy model.

That means material policy should feel like a sibling to technique policy, not
an unrelated mechanism:
- engine installs default family/profile/backend policy
- projects can override for target platforms
- recipes/views can bias quality/perf/portability
- games can still register custom families/backends and override defaults

The same overall rule should hold:

> engine defaults are strong, but the game can intervene at any level

Examples:
- project mobile profile forbids heavy surface layering
- VR view requires multiview-safe backends
- web build forces portable sampler counts
- a template may forbid unlit fallback for hero materials

### Fallback Model

Every family should define an ordered degradation ladder.

Example for `surface.standard`:

```text
surface.standard.visibility_buffer
-> surface.standard.deferred.gbuffer
-> surface.standard.forward.desktop
-> surface.standard.forward.mobile
-> surface.simple_lit
-> surface.unlit
```

Example for `sprite.lit`:

```text
sprite.lit.normal_mapped
-> sprite.lit
-> sprite.unlit
-> sprite.flat_color
```

Not every step must exist on every project.
The family just needs a coherent policy model.

### Required Vs Optional Features

Templates should be able to mark some features as:
- required
- preferred
- optional
- forbidden

That allows sane behavior like:
- a HUD material can safely fall back to flat color
- a physically-authored hero surface may refuse unlit fallback
- a web build can drop clear-coat while keeping the rest of the material valid

### Failure Mode

If no legal backend exists after applying policy:
- compile should fail for strict materials
- or compile should resolve to an engine-native error material for permissive
  policies

The result must be inspectable.

---

## Compile, Refresh, And Dirty Behavior

Materials must fit the compiled-frame lifecycle.

### Compile-Time Material Dirty

These changes may require backend re-resolution or plan recompile:
- template family/profile changed
- static feature set changed
- transparency/render domain changed
- backend registry changed
- capability profile changed
- technique variant changed
- material source schema/layout changed

### Refresh-Time Material Dirty

These changes should usually refresh only:
- scalar/vector parameter overrides
- texture rebinding within the same slot contract
- packed material table uploads
- instance parameter updates
- missing/loading texture resolution changes

### Source-Shape Interaction

If a source changes in a way that changes which material families or table
layouts are required, that is source-shape dirty and may escalate into topology
dirty for the plan.

This matches the existing lifecycle split:
- authored structure
- compiled topology
- cheap refresh
- source data mutation

---

## Material Registration And Game Extension

Games should be able to extend the material system at every level.

### Register A New Family

A game can register:
- a family name/handle
- parameter schema
- source compatibility
- default fallback ladder

Examples:
- `toon.surface`
- `water.volume`
- `card.character`

### Register Family Profiles

Within a family, a game can define profiles like:
- `toon.surface.ramped`
- `toon.surface.outlined`
- `toon.surface.hair`

### Register Backends

A game can provide backends for:
- built-in techniques
- custom techniques
- specific capability classes

Examples:
- `toon.surface.forward`
- `toon.surface.deferred`
- `toon.surface.visibility_buffer`

### Replace Or Disable Built-Ins

The engine should allow:
- replacing a built-in family implementation
- overriding default fallback rules
- disabling backends that do not match project style or platform goals

This keeps built-ins strong without making them mandatory.

---

## Relationship To Views, Recipes, And Plans

### Views

Views may:
- attach sources that reference material instances
- attach explicit material sources/tables
- carry view-local policy that affects material resolution

Examples:
- VR multiview preference
- mobile-friendly overlay policy
- photo-mode quality override

### Frame Recipes

Recipes should not enumerate materials directly in the common case.

They express:
- which views exist
- which techniques are requested
- where outputs go

Material resolution happens underneath, inside the compiled plan, from the
sources visible to those views.

### Compiled Plans

Compiled plans should record:
- which technique variant was chosen
- which material backend class was chosen
- why that backend won
- which fallbacks were skipped
- which material tables/layouts were generated
 - which render domain / bucket classification was used

That is essential for tooling and for debugging platform fallbacks.

---

## Future Paths

The material model should already fit these future paths.

### Forward

Materials resolve to direct shading backends and ordinary draw-time bindings.

### Deferred

Materials resolve to gbuffer-encoding backends plus later lighting-class
metadata.

### Visibility Buffer / Mesh Shader

Materials resolve to compact IDs, tables, and late material resolve contracts.

### Web / Mobile

Materials resolve to portable backends with reduced sampler pressure, simpler
resource layouts, and more conservative fallback ladders.

### VR

Materials can declare whether they require:
- multiview-safe backends
- view-dependent screen-space history
- stereo-sensitive resolve behavior

This lets the planner reject or downgrade materials that are incompatible with a
VR view policy.

---

## Inspection

Debug tooling should expose:
- material family
- template/profile
- instance count
- selected backend
- fallback chain
- reason for selection
- strict-vs-permissive policy outcome
- generated material table or binding layout
- whether the last change forced compile or only refresh

If Melody wants modern rendering without mystery, this has to be visible.

---

## Summary

Materials are the semantic shading layer of Melody rendering.

They let the engine keep:
- sources about data ownership
- views about visibility
- techniques about execution
- recipes about frame intent
- plans about compiled results

while still giving both the engine and games a real place to define:
- what a material means
- how it falls back
- and how it becomes concrete on the current rendering path.
