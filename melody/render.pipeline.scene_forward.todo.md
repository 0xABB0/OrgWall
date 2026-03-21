# render.pipeline.scene_forward

## TODO

- Shared-target multi-view composition is still wrong. `scene_forward` needs a real contract with the frame loop for first-view vs later-view load ops so overlays and split-screen do not stomp each other.
- `view->camera.viewport` still needs to drive viewport/scissor consistently for every internal submission path. The unified technique should not regress split-screen just because mesh and sprite paths now live together.
- Mesh emission now supports multiple material bindings, but only at the whole-geometry-handle level. Add explicit submesh/index-range support so one source can emit many parts from one uploaded geometry without forcing asset pre-splitting.
- Mesh lighting is still fed by a hardcoded directional light in the shader/view upload path. `scene_forward` needs scene-owned light/environment input instead of hardcoded demo-era lighting truth.
- Alpha-mask materials work, but alpha-blend surfaces still need a real forward transparency path: separate queueing, sorting, and resolve/composition semantics.
- `scene_forward` is still only a scene renderer. Transparency resolve, scene composition, and post/effect chains must stay outside the canonical scene model, but they still need a clean integration path above this module.
