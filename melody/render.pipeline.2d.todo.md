# render.pipeline.2d

## TODO

- Shared-target rendering is currently wrong for multi-view composition. The pipeline always clears color/depth and always renders full-screen, so a later view overwrites earlier views instead of respecting priority overlays or split-screen. Define the contract with the frame loop and use the correct load ops for "first view on target" vs "subsequent view on same target".
- Consume `view->camera.viewport` when setting viewport/scissor. Right now the 2D pipeline ignores it and always rasterizes across the full target, which breaks split-screen and partial-window views.
- Tier 4 fallback is an `assert(false)` rather than a real degraded path. Implement the non-bindless/traditional draw path instead of crashing when descriptor indexing is unavailable.
