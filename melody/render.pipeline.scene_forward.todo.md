# render.pipeline.scene_forward

## TODO

- Shared-target multi-view composition is still wrong. `scene_forward` needs a real contract with the frame loop for first-view vs later-view load ops so overlays and split-screen do not stomp each other.
- `view->camera.viewport` still needs to drive viewport/scissor consistently for every internal submission path. The unified technique should not regress split-screen just because mesh and sprite paths now live together.
- Mesh emission now supports multiple material bindings, but only at the whole-geometry-handle level. Add explicit submesh/index-range support so one source can emit many parts from one uploaded geometry without forcing asset pre-splitting.
- `scene_forward` is still only a scene renderer. Transparency resolve, scene composition, and post/effect chains must stay outside the canonical scene model, but they still need a clean integration path above this module.
