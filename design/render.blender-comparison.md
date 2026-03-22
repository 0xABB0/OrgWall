# Blender to Melody Render Ownership Comparison

This note exists to compare Melody's evolving render architecture against Blender's draw / EEVEE structure.

The goal is not to copy Blender blindly.
The goal is to understand:

- what Blender assumes that Melody can also assume
- what Blender assumes that Melody should not assume
- which ownership splits are solid enough to adopt
- which parts need a Melody-specific design

This is a corrective document.
It exists because a recent Melody change mixed scene truth, environment approximation, and response policy into one fixed struct.
That direction is not aligned with the engine spirit.

## Relevant Blender files

- `source/blender/draw/intern/draw_manager.hh`
- `source/blender/draw/intern/draw_resource.hh`
- `source/blender/draw/intern/DRW_render.hh`
- `source/blender/draw/engines/eevee/eevee_instance.hh`
- `source/blender/draw/engines/eevee/eevee_pipeline.hh`
- `source/blender/draw/engines/eevee/eevee_world.hh`
- `source/blender/draw/engines/eevee/eevee_world.cc`
- `source/blender/draw/engines/eevee/eevee_light.hh`
- `source/blender/draw/engines/eevee/eevee_shadow.hh`
- `source/blender/draw/engines/eevee/eevee_film.hh`
- `source/blender/draw/engines/eevee/eevee_film_shared.hh`
- `source/blender/draw/engines/eevee/eevee_film.cc`

## What Blender does

### 1. The draw manager is not the home of world / film truth

Blender's `draw::Manager` owns per-resource data:

- matrices
- bounds
- object infos
- object attributes

Indexed by `ResourceHandle`.

It is an interface between scene data and viewport engines, not the home of world lighting, exposure, or post policy.

This is visible in:

- `draw_manager.hh`
- `draw_resource.hh`

### 2. World input is separate from film / response

Blender's `World` module owns world-side inputs and world rendering setup:

- world material handling
- world sun extraction / sync
- lookdev world substitution
- world volume sync

This lives in:

- `eevee_world.hh`
- `eevee_world.cc`

Blender's `Film` module owns output / response behavior:

- accumulation
- jitter / reprojection
- render/display extents
- background opacity
- exposure scale

This lives in:

- `eevee_film.hh`
- `eevee_film_shared.hh`
- `eevee_film.cc`

Important:

`exposure_scale` comes from `scene.view_settings.exposure` inside `Film`, not `World`.

### 3. Execution is split into explicit modules

EEVEE's `Instance` contains separate modules:

- `world`
- `lights`
- `shadows`
- `film`
- `camera`
- `pipelines`
- probes
- AO
- ray tracing
- motion blur
- etc.

This is visible in:

- `eevee_instance.hh`

And the pipeline layer is itself split by role:

- `BackgroundPipeline`
- `WorldPipeline`
- `WorldVolumePipeline`
- `ShadowPipeline`
- `ForwardPipeline`
- deferred paths

This is visible in:

- `eevee_pipeline.hh`

### 4. Blender is opinionated about scene structure

Blender assumes:

- a canonical `Scene`
- a canonical `World`
- `ViewLayer`
- `view_settings`
- engine-managed pipeline modules

These assumptions are strong and real.

## What Melody should adopt

### A. Keep the scene DB small and truthful

Blender reinforces the right direction:

- manager / scene DB owns canonical authored truth
- not renderer approximations
- not film / response policy
- not stage-local derived data

So in Melody:

- `render.manager` / `render.scene` should not become the home of exposure, tonemap, or technique-shaped environment approximations

### B. Split scene-owned input from view-owned response

Blender's split is valuable:

- world input is world-side
- exposure / response is film-side

So in Melody:

- scene-owned lighting / environment input belongs on the scene side
- exposure / tone / output response belongs on the view / output side

### C. Split execution into explicit modules / stages

Blender does not keep everything in one "pipeline does all" blob.

Melody should take this lesson seriously.

For a forward scene technique, the chain should be explicit:

- visibility
- shadow preparation
- shadow rendering
- light selection
- main shading
- transparency
- resolve / output response

Each stage should have a contract.

## What Melody should *not* copy directly

### A. Do not require a Blender-style `World` datablock

Blender can assume a fixed world asset model.
Melody should stay more open.

Melody may need:

- a scene environment source reference
- a sky / background material source
- probe collections
- app-defined world contributors

So:

- copy the ownership split
- do not copy the exact data model

### B. Do not make response policy scene-global

Even though Blender stores exposure under scene view settings, Melody should allow multiple views of the same scene to diverge.

Examples:

- main game camera
- minimap
- monitor-in-scene
- reflection capture
- editor/debug view

So:

- response belongs with the observing view or output chain
- not the scene

### C. Do not hide extension points inside engine-private modules

Blender's modules are explicit, but not designed around "the app can hook in at any step."

Melody wants more openness.

So:

- adopt staged modules
- but expose hook seams more deliberately than Blender does

## Direct ownership table

| Concern | Blender owner | Melody equivalent | Should Melody copy this? | Notes |
|---|---|---|---|---|
| per-instance transforms / bounds / infos | `draw::Manager` | `render.manager` / scene DB | yes | Canonical per-instance truth, not renderer approximation |
| world material / sky input | `World` module | scene-owned environment input | yes, in spirit | Do not hardcode one environment model |
| direct light lists | `LightModule` fed from scene/world | scene-owned light inputs, consumed by technique | yes | Scene truth -> technique cache |
| shadows | `ShadowModule` | technique shadow stage/module | yes | Derived execution, not scene truth |
| exposure / film response | `Film` | view/output response stage | yes | Must not live in scene truth |
| background opacity / output combine policy | `Film` / background pipeline | view/output stage | yes | Output policy, not scene truth |
| environment approximation like sky-ground hemi | technique/world shader convenience | technique stage-derived approximation | yes | Not canonical scene truth |
| render passes / stage orchestration | pipeline modules | technique stages/modules | yes | Explicit stage seams |
| app hooks at arbitrary stages | weak / engine-internal | should be first-class in Melody | no, Blender does not solve this for us | Melody-specific design work |

## Corrective conclusion

The following Melody direction is wrong:

- scene-owned fixed struct mixing:
  - ambient
  - sky
  - ground
  - exposure

Why it is wrong:

1. It mixes scene truth and response policy.
2. It hardcodes one environment approximation.
3. It closes over future world/environment models.
4. It is not the ownership split Blender actually uses.

## Forward-facing Melody design direction

### 1. Scene side

Scene-owned authored inputs should include:

- light emitters
- shadow intent
- environment source reference or environment contributors
- later: probes, atmosphere, fog/media, volumes

Not:

- exposure
- tonemap
- fixed sky/ground approximation

### 2. View side

View-owned response/output inputs should include:

- camera
- target
- exposure policy
- output response / tonemap policy
- view-specific overrides

### 3. Technique side

Technique stages derive execution data from scene + view truth:

- visibility
- shadow setup
- shadow render
- environment/light selection
- shading
- transparency
- resolve / response

### 4. Hook seams

Melody should allow app / engine code to hook at stage boundaries:

- before visibility
- before shadow setup
- before main shading
- before transparency resolve
- before final output response

This is where Melody should exceed Blender.

## Immediate implication for current Melody code

The current `scene_environment` direction should not be treated as the final architecture.

At minimum:

- `exposure` must move out of `render.scene`
- any fixed `sky/ground` pair must not be treated as canonical truth

Before more lighting work lands, Melody should define:

1. scene-owned environment input model
2. view-owned response/output model
3. technique stage seam model

Only then should `M3` be finalized honestly.
