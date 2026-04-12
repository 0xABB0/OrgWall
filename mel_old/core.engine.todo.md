# core.engine

## TODO

- The frame loop is still swapchain-only. It skips any render target that is not a window target, so offscreen and array targets are not part of the automatic render scheduling path yet. Generalize frame scheduling to operate on render targets, not just swapchains.
- Independent cadences are not implemented yet. The frame loop renders every active window target every frame instead of deciding which views are due based on their target cadence.
- Multi-view composition needs an explicit engine-level contract. Right now views are sorted by priority and drawn in order, but there is no "first view clears / later views load" coordination, so shared-target composition is fragile even before pipeline-specific fixes.
- Window close event ordering is still sharp. The engine can destroy a window during `mel_process_event()` and then still forward the same SDL event to app/demo code. That makes stale window-handle use too easy in the same callback and should be made explicit or made impossible at the engine boundary.
