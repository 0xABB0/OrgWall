# GPU RHI M2 — Phase A (recording / state / rendering backbone, Vulkan)

Continues the GPU RHI rewrite (`design/gpu-rhi.md`). M1 landed the device foundation + a render-pass
skeleton. M2 is full Tier-2/3 resources + recording + rendering on Vulkan **and** D3D12; D3D12 is not
buildable on this host, so this slice is the Vulkan runnable half, sequenced in dependency order
(`design/gpu-m2.md`, now folded here). Phase A delivers the load-bearing backbone — **U10 textures/views,
U17 resource-state barriers, U15 standalone TLS command lists, U16 dynamic rendering** — and re-lowers the
engine onto dynamic rendering **without touching app source**. The new headless render test gives
machine-verified pixels, which M1 could not.

## Work done

- **U10 textures + views** (`gpu/texture.h`, `src/vulkan/texture.c`). `Mel_Gpu_Texture` /
  `Mel_Gpu_Texture_View` value handles, one slotmap each on the device, leak-reported at destroy.
  `texture_create` (kind/extent/layers/format/mips/samples/usage/cube), `texture_view_create`
  (view-dimension + aspect enum {Color/Depth/Stencil/DepthAndStencil/Plane0-2} + subresource range +
  format reinterpret), `texture_default_view`, destroy/alive. `texture_write` as the portable upload
  (staging buffer → `vkCmdCopyBufferToImage` with layout transitions; leaves the image in
  `ShaderResource`). The view owns the (future) bindless slot per §3.1; the indirect family and the heap
  are U14 (later).
- **U17 resource-state barriers** (`gpu/state.h`, `src/vulkan/record.c`). The full D3D12-style
  `Mel_Gpu_Resource_State` enum is carried (so sparse/video/ML/AS land later without touching every
  transition site, §7.3 / MEL-ENGINE-VII); the load-bearing subset is lowered
  (Common, ShaderResource, UnorderedAccess, RenderTarget, DepthWrite/Read, Copy/Resolve Source/Dest,
  Present, ShadingRateSource). `cmd_texture_barrier(cmd, tex, range, src, dst)` maps each state to a
  (stage, access, layout) triple; `Common`-as-source lowers to `UNDEFINED` (discard). Unimplemented
  states fall to GENERAL/all-access with a logged warning at the call site (§7.3 sanctioned fallback).
  `cmd_copy_texture_to_buffer` for readback.
- **U15 standalone command lists** (`gpu/command.h`, `src/vulkan/record.c`). `command_list_create(queue)`
  / `_begin` / `_end` / `_destroy`, recording a one-time-submit primary CB from a **per-thread
  per-queue-family command-pool registry** on the device (real TLS pools keyed by
  `mel_thread_current_id()`, mutex-guarded, destroyed at device teardown). Submitted through the existing
  `queue_submit → future` (U7). The swapchain's embedded frame recorder stays for the app path.
- **U16 dynamic rendering** (`gpu/rendering.h`, `src/vulkan/record.c` + `device.c` + `pipeline.c` +
  `command.c` + `swapchain.c`). `VK_KHR_dynamic_rendering` is probed and enabled when present;
  `vkCmdBeginRenderingKHR`/`EndRenderingKHR` are loaded via `vkGetDeviceProcAddr` and `dev->dynamic_rendering`
  gates the lowering. `cmd_begin_rendering` takes color (+optional depth) attachments with per-attachment
  load/store/clear. Pipelines build against `VkPipelineRenderingCreateInfo` when granted, render pass when
  not (§7.2 floor). The engine's existing `cmd_begin_pass`/`end_pass` + swapchain were re-lowered onto
  dynamic rendering with explicit UNDEFINED→ColorAttachment→Present image barriers; the render-pass path is
  retained as the `else` branch. **triangle/cube/lorenz source is unchanged.**

## Correctness pass (follow-up within the session)

Three correctness gaps in the Phase-A backbone were then closed:

- **Engine-side per-command-list, per-subresource state tracking (U17, §7.3).** Each command list records
  the state of every subresource a barrier touches; `cmd_texture_barrier` validates the declared source
  state against the tracked state and fires a loud `mel_log_error` + `mel_assert` on mismatch (the
  single most common single-thread→multithread porting bug), then records the destination. First touch
  accepts the declared source (the resource's external/initial state). Reset per recording.
- **Future-gated deferred retirement (U3, §3.3).** A device-wide submission **serial watermark** fed by
  *both* submission paths — `queue_submit` and the swapchain frame submit. Every owned
  buffer/texture/view/pipeline `*_destroy` now hands its Vulkan objects + memory to a deferred-free queue
  tagged with the most-recent submitted serial; they are freed only once the watermark passes that serial
  (advanced by the submit poller on the async path, by the synchronous wait on the no-reactor path, and by
  `frame_begin` after each in-flight fence resolves). The handle generation still rolls immediately, so
  use-after-free stays a loud `alive()` failure; only the underlying object free is deferred. Flushed at
  device destroy after `vkDeviceWaitIdle`. (Shader modules and semaphores are not CB-referenced after
  pipeline creation, so they stay immediate.) Single graphics queue ⇒ in-order completion ⇒ the `max()`
  watermark is exact; multi-queue will need per-queue watermarks.
- **Buffer-state barriers + the P2 fine escape.** `cmd_buffer_barrier(cmd, buf, src, dst)` lowers buffer
  states (vertex/index/constant/UAV/indirect/copy) to `VkBufferMemoryBarrier`. The synchronization-precision
  escape `mel_gpu_vk_cmd_image_barrier(...)` with native stage/access/layout lives in `gpu/vulkan/interop.h`
  (keeps raw Vulkan types out of the backend-clean core) and bypasses tracking — caller owns correctness.

## Verification

- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan` —
  **12/12** (the M1 eight plus `vk_texture.create_view_and_alive`, `vk_render.offscreen_clear_readback`,
  `vk_texture.write_and_readback`, `vk_render.buffer_barrier_submits_clean`). The offscreen test renders a
  clear into an RGBA8 texture via a standalone command list + dynamic rendering + barriers, copies
  image→READBACK buffer, and asserts the CPU-read pixel equals the clear colour (0.25/0.5/0.75/1.0 →
  ~64/128/191/255). The barrier sequence exercises the state tracker's first-touch and found-match-update
  paths; destroys exercise the deferred-free path; device-destroy reports no leaks. **Zero validation errors.**
- `./nob test gpu-foundation` — **8/8** (host).
- `HELLO_GPU_AUTO=triangle|cube <hello-gpu>` on the M3 Pro — device reports `render lowering: dynamic
  rendering`, `VK_KHR_dynamic_rendering` enabled, swapchain built (3 images), frames rendered for seconds
  with **zero VUID/validation errors, no device-lost, no crashes**. (atexit teardown not exercised here —
  process was signal-killed; leak detection still runs in the gpu-vulkan tests on `device_destroy`.)

## Kludges and debt (confessed, MEL-ENGINE-VIII)

- **Barriers use legacy `vkCmdPipelineBarrier`, not synchronization2.** Equivalent for the states lowered;
  the sync2 / `VK_KHR_unified_image_layouts` fast path (§7.3) is deferred. Avoided the sync2-enablement +
  MoltenVK-portability question for this slice.
- **State tracking is per-recording, not cross-CL/cross-submit.** The tracker (above) validates within one
  command list and resets at `begin`; a texture left in some state by CL-A must still have its `src`
  declared correctly in CL-B (the engine cannot know across CLs — that is the U20 render graph's job).
  `texture_write` documents its post-state (`ShaderResource`) for exactly this reason.
- **`Mel_Gpu_Resource_State` is an enum** (MEL-CODE-001). Justified as the D3D12/Vulkan protocol mapping
  and the shape pinned by the spec — same carve-out and Rule-#1 flag as M1's status/role/format enums.
- **Two render paths carried** (dynamic rendering + render pass), branched on `dev->dynamic_rendering`.
  The spec's §7.2 floor requires the render-pass fallback; this is faithful lowering, not a kludge, but it
  is duplicated surface in `pipeline.c`/`command.c`/`swapchain.c` until the floor can be retired.
- **`texture_write` is staging-only.** Host-image-copy (`VK_EXT_host_image_copy`, the §6.2 fast path and
  `HOST_TRANSFER` flag) is cap-gated and deferred; the `granularity_hint` warning is not emitted. Block-
  compressed alignment asserts are N/A (no BCn/ASTC formats in the enum yet).
- **`texture.memory` role is ignored** — textures are always device-local optimal; UPLOAD/READBACK roles
  log a warning and proceed device-local (linear host-visible textures are a later slice). Not a silent
  default (MEL-CODE-007): it warns.
- **Destroy ordering is still the caller's contract.** Deferred-free preserves the order the user
  destroyed in; a `VkImageView` must still be destroyed before its parent `VkImage` (Vulkan valid usage),
  and resources destroyed mid-record-but-pre-submit are gated only on already-submitted serials. Leak
  detection catches anything left live at device destroy.
- **Command list is still a heap pointer, not a value handle** (spec §3.1). M1 confessed this; unchanged.
  The TLS pool registry is real, but pools are never reset/recycled mid-run — each `command_list_destroy`
  frees its CB back to the pool; pools live until device teardown.
- **Retirement watermark assumes a single graphics queue** (in-order completion). Correct today; multi-queue
  needs per-queue watermarks or a min-across-queues completion barrier.
- **Swapchain/Surface remain opaque pointers** (M1 carryover). U18 value-handle promotion +
  `Mel_Platform_Surface` (`design/platform-surface.md`) deferred — that module does not yet exist.
- **Per-image present GC discipline unchanged from M1** (still frame-index-cycled binary semaphores, not
  `presentFenceInfo`-gated). Full U18 owns that.

## Deferred M2 slices (sequenced for follow-on sessions, not watered down)

In rough dependency order: U9 (transient ring + per-resource creation futures + `queue_sharing`); U11
(sampler dedup + immutable samplers); U13 (full pipeline state — blend/stencil/depth-bounds, GPL,
`pipeline_binary` cache + defensive load, `shader_object`, cache-control flags, hot-reload event, compute
pipelines); U14 (bindless descriptor heaps + root records + the `melody.binding` Slang mixin); U12 (Slang
offline compile + bundle + reflection; raw-bytecode peer already exists); U16 remainder (sub-passes /
tile-local read, VRS, foveation, multiview, feedback loops, MSAA resolve modes); U17 sync2 +
`unified_image_layouts` + timeline-driven cross-queue; U18 (value-handle swapchain + `Mel_Platform_Surface`
+ HDR + present timing + fullscreen). D3D12 co-primary (not buildable here).

## CLAUDE.md / repo-convention suggestions (recommendations only)

- **The `size`-typedef trap bit again** (a `usize size` parameter inside a function using `mel_assert`
  expands `countof`'s `(size)(...)` cast into a call). This is the second writeup to flag it. A hygienic
  `countof` or renaming the `isize` typedef away from `size` would end a recurring, baffling failure.
- **`./nob` is per-worktree.** `origin/main` lagged local `main` by one commit (`e129fad`), so a fresh
  worktree branched from the stale `origin/main`; I reset it to the local M1-complete commit. Worth noting
  in the session-start notes that the worktree base may trail local `main`.

## Suggestions

- Retire the render-pass floor on Vulkan once the support matrix is confirmed dynamic-rendering-capable
  everywhere Melody ships; carrying both paths is the duplication MEL-ENGINE-IX warns against, justified
  only by the §2 1.2-floor commitment.
- The per-CL state tracker (now landed) is the natural seam for the U20 render graph's auto-barriers and
  for cross-CL state inference; extend it there rather than re-deriving it.
- Consider a `modules/gpu/readme.md` (the repo convention) summarising the RHI surface; none exists yet.
