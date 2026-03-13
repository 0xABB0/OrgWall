# Render Graph: Multi-Swapchain Support

Parent: `task.window-decoupling.md` (step 4), `engine.render.graph.md`

Trigger: multi-window fractal demo (example.jobs.c) — Mandelbrot in window A, Julia set in window B, both driven by the same render graph in the same frame.

## Problem

`mel_render_graph_execute` is hardcoded to a single swapchain:
- `find_swapchain_target()` returns the first swapchain it finds across all passes
- One acquire, one command buffer, one submit, one present
- `khr_present()` bundles barrier recording + vkEndCommandBuffer + vkQueueSubmit + vkQueuePresentKHR into a single vtable call

Multiple passes can already target different swapchains via `write_targets`, but the execution pipeline only handles one.

## Design

### Swapchain vtable changes

Current:
```c
struct Mel_Swapchain_Vtable {
    bool (*acquire)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    bool (*present)(Mel_Swapchain* sc, Mel_Gpu_Device* dev, VkCommandBuffer cmd, VkFence fence);
    void (*resize)(...);
    void (*shutdown)(...);
    VkImageLayout (*current_image_layout)(...);
};
```

New:
```c
struct Mel_Swapchain_Vtable {
    bool (*acquire)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    void (*prepare_present)(Mel_Swapchain* sc, VkCommandBuffer cmd);
    void (*collect_sync)(Mel_Swapchain* sc, Mel_Gpu_Submit_Gather* gather);
    void (*present)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    void (*resize)(...);
    void (*shutdown)(...);
    VkImageLayout (*current_image_layout)(...);
};
```

All new entries are nullable. Non-GPU swapchain types leave them NULL.

`prepare_present` — record layout transition barrier into the command buffer. For GPU window swapchain: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR. Called after all passes have executed, before the command buffer is ended.

`collect_sync` — push wait/signal semaphores into a gather struct. For GPU window swapchain: push image_available as wait, render_finished as signal. The render graph owns the gather struct and does the actual vkQueueSubmit.

`present` — do post-submit work. For GPU window swapchain: just vkQueuePresentKHR. For video swapchain: write frame to file. For image swapchain: no-op. No longer takes cmd or fence — submit is the graph's responsibility.

### Submit gather struct

```c
typedef struct {
    VkSemaphore wait_semaphores[MEL_MAX_SWAPCHAINS];
    VkPipelineStageFlags wait_stages[MEL_MAX_SWAPCHAINS];
    u32 wait_count;
    VkSemaphore signal_semaphores[MEL_MAX_SWAPCHAINS];
    u32 signal_count;
} Mel_Gpu_Submit_Gather;
```

Fixed-size arrays. MEL_MAX_SWAPCHAINS = 8 or so. If someone needs more than 8 simultaneous swapchains they can bump it. No dynamic allocation in the submit hot path.

### Render graph execution (new flow)

```
1. Collect all unique swapchains from all passes' write_targets
2. For each swapchain: acquire(sc, dev)
   - If acquire fails (minimized window), mark swapchain as skipped
   - Passes targeting a skipped swapchain are excluded from execution
3. Wait on frame fence, reset command pool, begin command buffer
4. Execute passes (unchanged, but skip passes whose swapchain was skipped)
5. For each acquired swapchain: prepare_present(sc, cmd)
6. End command buffer
7. Gather sync:
   Mel_Gpu_Submit_Gather gather = {0};
   for each acquired swapchain: if (collect_sync) collect_sync(sc, &gather)
8. vkQueueSubmit(cmd, gather.waits, gather.signals, fence)
9. For each acquired swapchain: present(sc, dev)
10. Advance frame index
```

### GPU window swapchain changes (gpu.swapchain.c)

`khr_present` is split into three functions:

`khr_prepare_present(sc, cmd)`:
- Record the IMAGE_MEMORY_BARRIER_2 for PRESENT_SRC transition
- Nothing else

`khr_collect_sync(sc, gather)`:
- Push image_available[current_frame] into gather->wait_semaphores
- Push COLOR_ATTACHMENT_OUTPUT_BIT into gather->wait_stages
- Push render_finished[current_image] into gather->signal_semaphores

`khr_present(sc, dev)`:
- vkQueuePresentKHR with render_finished[current_image]
- Update image_layouts
- Advance current_frame

The old `khr_present(sc, dev, cmd, fence)` is removed.

### Skipping minimized windows

When `acquire` returns false for a swapchain (window minimized, out-of-date, etc.), that swapchain is "skipped" for this frame. Passes that ONLY target skipped swapchains are not executed. Passes that target both a skipped and a live swapchain still execute (they write to the live one; the skipped target is simply not presented).

This needs a per-swapchain "acquired" flag tracked during the collect phase.

## Migration

1. Add `prepare_present`, `collect_sync` to vtable, implement for khr swapchain
2. Change `present` signature: remove cmd + fence params
3. Implement new `present` for khr (just vkQueuePresentKHR)
4. Update `mel_render_graph_execute` to the new flow
5. Remove old `khr_present`
6. `find_swapchain_target` -> `collect_swapchain_targets` (returns array + count)
7. Swapchain image headless (swapchain.image.c) — implement prepare_present if needed, leave collect_sync and present as NULL
8. Update all call sites of `mel_swapchain_present` macro

## Not in scope

- Per-window frame cadence (different refresh rates) — requires threaded frame execution
- Multiple render graphs — still one graph, but it can target N swapchains
- Cross-swapchain dependencies (pass reads from swapchain A's previous frame into swapchain B) — not needed, declare if it comes up
