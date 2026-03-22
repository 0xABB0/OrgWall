# Sponza Completion Roadmap

This file is the technical completion roadmap for `demos/sponza`.

It is ordered.
We work from top to bottom.

When the last milestone in this file is complete, Sponza is complete for the engine features that this scene should legitimately exercise.

## Completion target

Sponza is complete when all of the following are true:

- scene import is correct for the Khronos Sponza asset we ship against
- material response is correct for the material features Sponza actually uses
- direct lighting and ambient/environment response are correct enough for the scene to read truthfully
- shadows are stable, believable, and chosen from the best supported path at startup
- transparency is correct for the scene’s needs
- visibility and submission are GPU-driven when the active device supports it
- CPU steady-state per-frame work is near-zero except for high-level control and unavoidable orchestration
- weaker hardware gets an explicit, correct degraded path
- no core renderer feature exists only as a Sponza-specific hack

## CPU/GPU target

Target on capable hardware:

- CPU:
  - startup / shutdown
  - asset loading
  - input
  - camera/controller update
  - high-level scene mutation
- GPU:
  - visibility
  - draw generation / submission preparation
  - steady-state scene rendering
  - shadow work

Fallback target on weaker hardware:

- CPU may perform more visibility and submission work
- but the fallback must be explicit, correct, and chosen at startup from capabilities

## Source of truth rules

1. Scene truth stays in scene / manager / source / material systems.
2. Execution strategy is chosen by the renderer technique from device capabilities and policy.
3. Demo-local helpers stay demo-local until proven general enough to graduate into engine modules.
4. A milestone is not complete if it only works on one hardware class without an honest degraded path.

## Milestone status legend

- `not started`
- `partial`
- `complete`

## M0. Demo foundation

Status:
- complete

Goal:
- the demo is a stable pressure scene, not a fragile experiment

Engine modules exercised:
- `core.app`
- `core.engine`
- `window`
- `render.viewport`
- `render.target`

Demo modules:
- `demo.sponza.c`
- `sponza.camera.*`
- `sponza.scene.*`
- `sponza.loader.*`

Required work:
- stable startup
- stable shutdown
- stable resize
- robust input/camera loop
- no stale window/event lifetime misuse
- no planning/documentation drift

Exit criteria:
- demo runs, resizes, and closes cleanly
- no demo lifecycle bug is still known
- docs/todos reflect reality

Notes:
- a broader engine-level sharp edge still exists around window-close event ordering and is tracked in `core.engine.todo.md`
- that issue no longer leaks into demo-local misuse in Sponza, so it does not block M0 completion here

## M1. Asset ingestion for this scene

Status:
- complete

Goal:
- the asset path for Khronos Sponza is fully correct for what this scene actually needs

Engine modules exercised:
- `gpu.geometry_pool`
- `render.source.manual`
- `render.material_base`
- `texture`
- `texture.pool`

Demo modules:
- `sponza.loader.*`

Required work:
- static mesh ingestion
- per-primitive material assignment
- correct root transform handling
- correct bounds derivation
- all required texture channels ingested correctly
- honest handling of every glTF feature this asset actually uses

Exit criteria:
- no important Sponza scene component is silently skipped
- no required glTF material feature used by this asset is silently ignored
- import produces canonical engine truth, not renderer-shaped hacks

Graduation candidates after this milestone:
- reusable glTF ingestion helpers
- reusable material translation helpers

Notes:
- the current Khronos Sponza asset shape is now explicitly validated by the loader
- unsupported used glTF features now fail loudly instead of being silently ignored
- this milestone is complete for this scene, not a claim that we now have a general glTF scene importer

## M2. Material correctness

Status:
- complete

Goal:
- all material behavior Sponza relies on is rendered correctly

Engine modules exercised:
- `render.material_base`
- `render.pipeline.scene_forward`
- `texture.pool`
- shaders

Required work:
- base color
- normal maps
- metallic-roughness
- alpha mask
- alpha blend where needed
- double-sided raster state
- emissive if required by the asset
- occlusion if required for scene truthfulness

Exit criteria:
- no major surface class in Sponza is obviously wrong
- material truth lives in materials, not in manager hacks
- renderer handles material execution differences without demo-only branches

Notes:
- for the current Khronos Sponza asset, the materially relevant features are base color, normal maps, metallic-roughness, alpha mask, and double-sided materials
- occlusion, emissive, and alpha-blend are not used by this asset and therefore do not block M2 completion for Sponza
- double-sided backface lighting is now handled in the forward shader so the masked/double-sided materials in this asset read correctly from both sides

## M3. Lighting model

Status:
- complete

Goal:
- Sponza is lit by a scene-owned lighting model that is sufficient for this scene

Engine modules exercised:
- `render.scene`
- `render.pipeline.scene_forward`
- shaders

Demo modules:
- `sponza.scene.*`

Required work:
- scene-owned direct lighting
- environment / ambient model strong enough for believable scene read
- exposure / response control if needed
- no hidden hardcoded technique light

Exit criteria:
- lighting truth is scene-owned
- renderer derives light execution data from scene truth
- the scene no longer depends on one brittle hand-tuned direct-light arrangement to be readable

Graduation candidates after this milestone:
- scene-light authoring helpers if they prove general

Notes:
- scene-owned environment now includes ambient, sky, ground, and exposure
- `scene_forward` consumes scene lighting truth instead of carrying hidden technique-owned light state
- Sponza now authors its environment and light setup through `sponza.scene.*`

## M4. Shadowing

Status:
- partial

Goal:
- shadows are structurally correct, stable, and believable

Engine modules exercised:
- `render.scene`
- `render.pipeline.scene_forward`
- `gpu.pipeline`
- `gpu.texture`
- shaders

Startup strategy choice required:
- choose the best supported shadow execution path for the active device

Required work:
- explicit shadow-space convention
- stable directional shadow fit
- better biasing and filtering
- if needed for scene quality: cascaded directional shadows
- honest fallback when richer shadow paths are unsupported

Exit criteria:
- no implausible large-scale shadow artifacts at normal or unusual viewpoints
- shadows remain stable while moving through the scene
- renderer chooses its shadow path at startup from capabilities and policy

Graduation candidates after this milestone:
- shadow fit helpers
- shadow resource helpers
- strategy-selection helpers

## M5. Transparency

Status:
- partial

Goal:
- transparency behavior needed by Sponza is correct

Engine modules exercised:
- `render.pipeline.scene_forward`
- `render.material_base`
- shaders

Required work:
- alpha mask correctness in main pass
- alpha mask correctness in shadow pass
- alpha blend correctness if the scene materially needs it
- explicit transparency cost/order model

Exit criteria:
- no important transparent material class in Sponza is obviously wrong
- transparency behavior is explicit, not accidental

## M6. Visibility

Status:
- partial

Goal:
- visibility for Sponza is structurally correct and scales with scene size

Engine modules exercised:
- `render.pipeline.scene_forward`
- future visibility/culling modules if introduced

Startup strategy choice required:
- choose GPU-driven visibility when supported
- choose an honest CPU path when not

Required work:
- define the visibility policy clearly
- avoid avoidable CPU whole-scene submission work
- move toward GPU-driven visibility on capable hardware
- keep fallback explicit on weaker hardware

Exit criteria:
- on capable hardware, Sponza visibility is GPU-driven or mostly GPU-driven
- on weaker hardware, fallback is correct and explicit
- CPU no longer does avoidable steady-state whole-scene visibility work

This milestone is the primary one behind:
- “100% GPU-driven when applicable”
- “near-zero CPU work”

## M7. Submission strategy

Status:
- not started

Goal:
- Sponza is rendered through the best supported scene submission strategy on the active device

Engine modules exercised:
- `render.pipeline.scene_forward` or its successor
- GPU submission / indirect / mesh-shader support as applicable

Startup strategy choice required:
- yes, explicitly

Possible strategies:
- classic direct draws
- indirect GPU-driven draws
- compute-assisted submission
- mesh-shader path

Required work:
- define strategy choice cleanly
- choose it at startup
- expose/debug the choice
- keep scene truth independent from strategy

Exit criteria:
- strategy is chosen once from capabilities and policy
- strategy choice is visible and debuggable
- scene data model does not change shape based on the chosen execution path

## M8. Performance discipline

Status:
- not started

Goal:
- the final Sponza path is not only correct, but cost-honest

Engine modules exercised:
- all active renderer pieces for this scene

Required work:
- measure upload volume
- measure visibility cost
- measure submission cost
- measure shadow cost
- measure material/texture binding pressure
- remove always-on costs that are not justified

Exit criteria:
- no major steady-state cost exists without justification
- major costs correspond to active features
- startup strategy choices are supported by measured cost, not taste

## M9. Engine graduation pass

Status:
- not started

Goal:
- move proven general-purpose demo modules or helpers into the engine

Candidates:

1. `sponza.camera.*`
- likely as a general fly/debug camera module if the interface is negotiated properly

2. reusable parts of `sponza.loader.*`
- not the whole Sponza-shaped loader
- only helpers that proved general and honest

3. scene-light authoring helpers discovered in `sponza.scene.*`

4. shadow/strategy helpers proven during Sponza work

Exit criteria:
- anything left in `demos/sponza/` is truly demo-local
- anything general-purpose has an engine home or a documented reason not to move yet

## M10. Completion review

Status:
- not started

Goal:
- verify that Sponza is actually complete

Exit criteria:
- M0 through M9 are complete
- no known renderer limitation still materially compromises this scene without being intentionally out of scope
- Sponza is a trustworthy engine reference scene

## Notes

- The roadmap order is strict from this point on.
- Earlier pressure-testing may have already touched later milestones. That does not mark them complete.
- If a new engine pain point appears, fix it or write the module-local todo immediately.
