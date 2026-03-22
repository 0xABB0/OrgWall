# render.pipeline.scene_forward

## TODO

- Shared-target multi-view composition is still wrong. `scene_forward` needs a real contract with the frame loop for first-view vs later-view load ops so overlays and split-screen do not stomp each other.
- `view->camera.viewport` still needs to drive viewport/scissor consistently for every internal submission path. The unified technique should not regress split-screen just because mesh and sprite paths now live together.
- Mesh emission now supports multiple material bindings, but only at the whole-geometry-handle level. Add explicit submesh/index-range support so one source can emit many parts from one uploaded geometry without forcing asset pre-splitting.
- `scene_forward` now consumes scene-owned ambient, directional, and point lights through explicit buffers. When richer world/environment input arrives, derive renderer approximations here or in dedicated stage modules instead of baking fixed approximations into scene truth.
- Directional shadow support is now present, but it is intentionally narrow: one shadow-casting directional light per view, with the shadow map fitted from camera-visible mesh bounds. Keep point-light shadows and cascades out of the core path until they have a real home.
- Shadow-space conventions are still too implicit. The first shadow pass bug came from rendering the shadow map with a flipped viewport and initially sampling it as if it were unflipped. Make the projection/viewport/sample convention explicit so future passes do not have to rediscover it by breaking visibly.
- The current directional shadow fit is too view-dependent for extreme viewpoints. Fitting from camera-visible mesh bounds was acceptable to get an honest first pass in, but it can still create unstable or implausible coverage when the camera moves far outside the usual play space.
- Alpha-blend mesh materials now have a real forward pass, but the transparency model is still basic: bounds-center sorting, one blended queue, and no richer composition/resolve semantics yet.
- `scene_forward` is still only a scene renderer. Transparency resolve, scene composition, and post/effect chains must stay outside the canonical scene model, but they still need a clean integration path above this module.
- The current forward mesh shading path is now multi-light, but it is still a basic forward loop. If counts grow, move toward a deliberate visibility/light selection path rather than silently paying `mesh fragments x scene lights` forever.
- Scene-scale culling now exists as a basic CPU frustum test in the draw path. If we push this further, do it deliberately: shared visibility policy, optional world-space bounds caching if measurement justifies it, and a modern GPU culling path only when it actually earns its cost.
