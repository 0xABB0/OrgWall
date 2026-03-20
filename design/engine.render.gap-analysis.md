# Rendering Gap Analysis

This document captures the current state of Melody's rendering architecture,
what was fixed in the recent refactor, and what is still missing before the
rendering layer can be considered complete across a wide capability range:

- software rendering
- traditional raster
- indirect / GPU-driven raster
- mesh shaders
- future vendor-specific paths

This is not a feature wishlist. This is a structural gap analysis against the
engine's philosophy:

- respect
- correctness
- performance
- clearness
- no hacks
- no shortcuts
- only pay for what you use
- degrade gracefully

## What is now structurally correct

The recent refactor fixed the biggest architectural lie in the previous design.

We now have:

- one `Mel_Render_Manager` owned by one explicit `Mel_Render_Scene`
- `Mel_Render_Source` as a pure sync adapter into a manager
- `Mel_Render_View` rendering a scene instead of directly owning or implying scene state
- `Mel_Render_Pipeline_Scene` as a per-`(scene, pipeline type)` derived cache layer
- `Mel_Render_Pipeline` as a per-view persistent state object

This gives us the correct lifetime split:

- scene truth: `Mel_Render_Manager`
- technique-derived scene cache: `Mel_Render_Pipeline_Scene`
- view-local persistent state: `Mel_Render_Pipeline`

This is a much better foundation, but it is not yet a complete rendering layer.

## Final target

The final rendering layer should allow one canonical scene model to feed many
rendering techniques efficiently and honestly:

- a software renderer on weak or unusual targets
- a classic forward or deferred raster path
- an indirect GPU-driven path
- a mesh-shader path
- future specialized paths built on vendor SDKs or newer APIs

The canonical scene model must remain stable while each technique derives its
own execution data from it.

The architecture must not force all targets into the same physical layout, but
it must keep one authoritative semantic truth.

## Missing pieces

## 1. The manager is still schema-shaped instead of semantic

Right now a render scene is created with pool descriptors, so different scenes
still imply different manager layouts such as "2D pools" vs "3D pools".

That is good enough for the transitional architecture, but not for the final
one.

The manager should become a canonical semantic render database containing
concepts such as:

- object handle
- transform
- bounds
- geometry reference
- material reference
- light reference
- layer / visibility flags
- optional custom payload references

Techniques should derive their execution buffers from this semantic database
instead of defining the database shape themselves.

## 2. Pipeline-scene caches exist, but they are still shallow

`Mel_Render_Pipeline_Scene` now exists, but current pipelines only use it as a
place to move uploads and per-scene prep out of per-view draw.

That is useful, but still far from enough.

For a complete rendering layer, pipeline-scene caches should own real derived
data such as:

- compact object tables
- technique-specific material tables
- indirect command seeds
- meshlet descriptors
- acceleration structures
- software tile bins
- classification buffers
- compaction buffers

Without this, the cache layer exists structurally but does not yet carry its
full purpose.

## 3. Capability-driven degradation is not yet a first-class system

Supporting "old to modern" correctly means each technique needs an explicit
tier ladder.

Example:

- software path
- classic CPU-submitted raster path
- indirect GPU raster path
- bindless path
- mesh-shader path
- future specialized vendor path

These paths should not be accidental fallbacks or assert-based stubs. They
must be explicit technique choices selected by capability and cost.

The engine must degrade honestly:

- no pretending unsupported features are available
- no crashing because a lower tier was not implemented
- no forcing low-end targets to carry high-end costs

## 4. Same-target multi-view composition still needs a hard contract

The architecture now has better ownership, but a complete render layer still
needs explicit rules for:

- first view on target vs later views
- clear vs load behavior
- split-screen
- overlay composition
- alpha-over ordering
- stereo / multiview
- offscreen-to-onscreen composition

Without this, correctness depends too much on pipeline-local behavior.

The engine should define target composition rules once and let pipelines obey
them, not reinvent them.

## 5. Materials are not yet fully technique-agnostic

The current material system is still closer to:

- parameter buffer
- shader pointer
- compatibility bits

This is not enough for a complete rendering layer.

We still need:

- stable material handles
- safe material instance lifetime
- clear compatibility across techniques
- variant selection
- texture / sampler policy
- fallback behavior on lower tiers
- optional technique-specific translation

Materials should describe appearance and required data, not accidentally encode
one pipeline's preferred upload layout.

## 6. Lights, shadows, and world state are not yet first-class scene data

A complete rendering layer is more than object submission.

The canonical scene model still needs authoritative support for:

- directional lights
- point lights
- spot lights
- shadow-casting and shadow-receiving policy
- environment / sky / world
- probes
- camera-exposure-adjacent state where appropriate

Until this is explicit, rendering techniques can draw objects, but the engine
does not yet have a full scene renderer.

## 7. Transient resource management is still too ad hoc

A real rendering layer needs robust management of:

- temporary attachments
- pass-local intermediate resources
- aliasing opportunities
- lifetime tracking
- synchronization / usage transitions

This becomes increasingly important with:

- deferred shading
- post-processing chains
- shadows
- async compute
- multiview
- vendor-specific specialized paths

We need a more intentional transient resource and pass dependency model.

## 8. Offscreen and non-window rendering need to be fully first-class

If the engine is to support:

- texture baking
- editor previews
- thumbnails
- reflection captures
- VR eyes
- offscreen composition
- software targets

then rendering cannot remain mostly window-driven in spirit.

The frame scheduler should operate on render scenes, views, and targets in a
general way, whether the target is a window, an image, an array image, or a
CPU-owned buffer.

## 9. The backend abstraction still needs to prove itself under multiple techniques

The GPU abstraction is not truly validated by one or two rendering paths.

It must prove that it can support:

- software fallback integration where needed
- classic raster
- compute-heavy rendering
- indirect submission
- mesh shaders
- vendor SDK features
- web and mobile degraded paths

The public abstraction must remain:

- honest enough for performance
- abstract enough for portability
- not raw-Vulkan-shaped
- not uselessly generic

## 10. Tooling and debug visibility are still missing

A serious rendering layer needs the user to understand what is happening.

We still need strong support for:

- debug names
- pass markers
- pipeline / tier reporting
- culling debug views
- material fallback visibility
- resource inspection
- "why did this object not render?" workflows

This is not optional polish. It is part of respecting the user.

## 11. Handle stability and invalidation rules still need hardening

A full renderer needs explicit rules for:

- object handle lifetime
- material handle lifetime
- scene cache invalidation
- remapping after compaction
- source attach / detach behavior
- resize handling
- device-loss behavior where relevant

If these rules stay fuzzy, correctness becomes accidental.

## 12. The software rendering path is still only architectural

The current architecture now has a place for a software renderer, which is a
major improvement.

But the actual software rendering layer still does not exist.

That path will need:

- CPU target abstraction
- color / depth ownership rules
- rasterization pipeline
- culling / clipping policy
- texture sampling policy
- format conversion rules
- integration with the same canonical scene model

This is a real subsystem, not a small fallback.

## Recommended milestone order

The next major work should happen in this order:

1. Make the manager canonical and semantic rather than pool-schema-defined.
2. Make `Mel_Render_Pipeline_Scene` own real derived technique data.
3. Implement explicit capability tiers for each technique.
4. Define hard same-target composition rules for views.
5. Make materials, lights, shadows, and world data first-class.
6. Improve transient resource / pass dependency management.
7. Make offscreen and non-window rendering fully first-class in scheduling.
8. Implement a real software renderer technique.
9. Add strong debugging and diagnostics infrastructure.

## Verification questions

When making future rendering changes, we should keep asking:

1. Is the data in the manager semantic truth, or did a technique-specific
   layout leak into the canonical scene model?
2. Is this cost scene-level, view-level, or draw-level?
3. Does this feature degrade honestly on weaker hardware?
4. Are we paying for work that the user did not ask for?
5. Can this path support both very old and very modern techniques without
   architectural lying?
6. If this goes wrong, will the user understand what happened?

## Current conclusion

The current refactor gave Melody a much better rendering skeleton.

What is still missing is the full musculature:

- a canonical semantic scene model
- rich technique-scene caches
- real capability tiers
- composition rules
- first-class materials/lights/world
- transient resource management
- software rendering
- diagnostics

That is the path from "good architecture foundation" to "fully working
rendering layer".
