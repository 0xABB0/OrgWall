# demo.sponza plan

This is the working plan for the Sponza demo.

It is not a design manifesto. It is the build checklist and truth source we can keep coming back to while implementing.

## Goal

Build a real `demos/`-level Sponza scene that proves the current rendering layer can carry a serious static environment without violating the engine philosophy:

- respect
- correctness
- performance
- clearness
- only pay for what is actually used

The demo should become a renderer reference scene, not just an asset viewer.

## Why Sponza

Sponza is useful because it pressures:

- mesh ingestion
- many materials
- texture binding scale
- camera/navigation
- scene setup
- visibility/culling pressure
- lighting pressure
- overall renderer sanity

It is a good “real scene” without immediately dragging in character animation, gameplay, or editor concerns.

## Scope boundary

Sponza v1 is:

- static scene
- controllable camera
- correct mesh/material/texture import and display
- scene rendered through the current canonical scene model

Sponza v1 is not:

- animation showcase
- full post stack showcase
- gameplay demo
- benchmark harness

Those can come later.

## Non-negotiable rules

1. No ad-hoc renderer side paths just for Sponza.
2. No asset-specific hacks in core modules.
3. No hidden costs baked into the scene model.
4. If Sponza reveals architectural pain, we fix the architecture or write the todo in the right module.
5. If something is not used, it must not be allocated, uploaded, or processed.

## Demo structure

Target output:

- `demos/sponza/demo.sponza.c`

Supporting data may live under `assets/` if needed, but the demo code should stay self-contained and readable.

Current code layout:

- `demo.sponza.c`: app shell and lifecycle wiring
- `sponza.loader.*`: Sponza-shaped glTF/material import path
- `sponza.scene.*`: scene-authored lighting/layout setup
- `sponza.camera.*`: free-fly controller and camera event handling

This split is intentional so future renderer work such as shadows does not keep accumulating into one giant demo file.

## Current status

Completed:

- M0: Khronos glTF Sponza chosen and loaded directly from `assets/sponza/khronos/glTF/Sponza.gltf`
- M1: static geometry is on screen through `scene_forward`
- M2: base-color textures, alpha-mask materials, normal maps, metallic-roughness maps, and double-sided raster state are wired
- M3: one instance with many material bindings and many emitted mesh parts is exercised by the import path
- M4: free-fly navigation and startup controls are in place
- scene-owned lighting is now authored through `render.scene` and consumed by `scene_forward`, and Sponza now exercises ambient + directional + point light inputs instead of a hidden hardcoded light
- basic scene-scale frustum culling is now active in `scene_forward` for mesh submission
- directional shadow support is now active for the first shadow-casting directional light in `scene_forward`, with the shadow map fitted from camera-visible mesh bounds

Still open:

- M5 is still only partially addressed. Basic frustum culling is in, but broader visibility work and deeper performance tuning are still renderer follow-ups rather than a finished pass.
- M6 remains open for renderer-level follow-ups exposed by Sponza, especially richer shadow coverage, environment lighting, transparency quality beyond the current basic alpha-blend path, and deeper visibility/perf work.
- The demo loader is still intentionally Sponza-shaped, not a general glTF scene importer. It is acceptable for this demo scope, but it should not be mistaken for a generic asset pipeline.
- Startup currently does a lightweight glTF pre-scan to size the geometry pool to the actual asset. That is an explicit memory-vs-startup tradeoff, not a hidden cost.

## Milestones

## M0: asset decision and ingestion path

Decide:

- exact Sponza asset source we will use
- import format we will support first
- what preprocessing, if any, is required

Success:

- one clear asset path
- one clear import path
- no ambiguity about material/texture conventions

## M1: static geometry on screen

Load the Sponza scene and render it as static geometry.

Requirements:

- all meshes loaded
- transforms correct
- camera can navigate
- scene renders through `scene_forward`
- scene data authored through the current source/instance/material/space model

Success:

- Sponza is visible and navigable
- no placeholder geometry
- no hardcoded per-mesh special cases in renderer core

## M2: material correctness

Get the scene materials looking broadly correct.

Requirements:

- diffuse/base-color textures
- normal maps if supported by the active material path
- metallic-roughness textures if supported by the active material path
- alpha-tested surfaces where needed
- material instances correctly bound at scale

Success:

- curtains, banners, foliage, stone, metal read correctly
- the scene is materially differentiated, not flat-colored

## M3: scene-scale binding correctness

Use Sponza to pressure the new binding model.

Requirements:

- many instances and many material bindings behave correctly
- source emission remains canonical
- no regression to one-object-one-material assumptions

Success:

- import path can express the scene without structural hacks

## M4: navigation and usability

Turn it into a real demo rather than a static screenshot.

Requirements:

- free-fly camera
- basic input help
- predictable startup position
- stable resizing

Success:

- someone can launch the demo and inspect the scene immediately

## M5: performance pass

Only after correctness.

Look at:

- upload volume
- instance/material binding pressure
- texture binding pressure
- visibility/culling behavior
- avoid obviously wasted work

Success:

- no blatant whole-scene waste that violates the guidebook

## M6: renderer follow-ups exposed by Sponza

Possible examples:

- richer shadow coverage
- alpha-tested materials needing better treatment
- better submesh/material import
- culling improvements
- lighting/environment needs
- transparency improvements beyond the current basic alpha-blend path

Success:

- every discovered pain point becomes either:
  - a direct fix
  - or a module-local todo

## Acceptance criteria for “Sponza v1 done”

- launches as a `demo`
- loads a real Sponza asset set
- camera can move through the scene
- scene renders correctly enough to inspect materials and structure
- renderer architecture remains clean
- no demo-specific shortcuts in core render modules

Current read:

- satisfied for static inspection and navigation
- not a claim that scene lighting, blended transparency, or scene-scale culling are fully solved
- not a claim that shadows beyond the current single directional shadow map, probes, or richer environment lighting are solved
- not a claim that the demo loader is a general-purpose glTF scene importer

## Known likely pressure points

- scene import path for many meshes and materials
- texture/material asset loading
- alpha-tested geometry
- submesh/material mapping
- large scene startup cost
- keeping source payload honest instead of flattening everything too early

## Likely first implementation order

1. choose the asset package and import path
2. get static mesh placement rendering
3. wire material instances and textures
4. add fly camera and demo controls
5. do one cleanup pass on pain points revealed

## Notes while implementing

- Sponza is allowed to reveal missing engine pieces
- the answer to missing engine pieces is not demo-local hacks
- keep this file updated when the plan changes materially
- the loader may stay asset-specific for this demo, but that limitation must stay explicit
- memory-saving startup work such as pre-sizing pools is acceptable when it is deliberate, visible, and local to the demo
