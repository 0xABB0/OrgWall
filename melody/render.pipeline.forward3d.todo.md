# render.pipeline.forward3d

## TODO

- Shared-target rendering is currently wrong for multi-view composition. The pipeline always clears color/depth and always renders full-screen, so a later view overwrites earlier views instead of respecting priority overlays or split-screen. Define the contract with the frame loop and use the correct load ops for "first view on target" vs "subsequent view on same target".
- Consume `view->camera.viewport` when setting viewport/scissor. Right now the forward 3D pipeline ignores it and always rasterizes across the full target, which breaks split-screen and partial-window views.
- Material parameter binding only supports one bound material buffer at a time. The draw loop overwrites descriptor binding `2` for each compatible material base, so the last base wins. Redesign this path so a draw can resolve both `material_base_id` and `material_idx` correctly.
