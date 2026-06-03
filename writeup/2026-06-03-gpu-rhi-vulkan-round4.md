# 2026-06-03 — GPU-RHI round 4 (Vulkan): async query resolve, async transfer upload, shaderFloat64 grant + bug audit

Vulkan backend, off `4805d3b`. `gpu-vulkan` 40 → **48** green; `gpu-foundation` **8/8**; sibling suites unregressed (`gpu-stress` 20/20, `gpu-concurrency` 10/10, `gpu-visual` 11/11, `gpu-bench` 12/12). Zero `VUID`, zero leaks, zero unexpected validation errors (DYLD path + `MEL_TEST_NOFORK=1`, MoltenVK 1.4.1 on Apple M3 Pro). No comments added anywhere.

## Work done

### Task 1 — async query resolve (§11.2 / U3 §3.3)
The spec's portable resolve shape: `query_pool_resolve(pool, range) → future<results>` via `vkCmdCopyQueryPoolResults` into a readback buffer, future-gated on the submission that performs the copy. Round-3 shipped only the synchronous `vkGetQueryPoolResults(WAIT)` helper; this round adds the async peer and keeps the sync helper as the P2 escape.

- `include/gpu/query.h` (additive): `Mel_Gpu_Query_Resolve_Status`, `Mel_Gpu_Query_Resolve { const u64* ns; u32 count; }`, `mel_gpu_query_pool_resolve_async(dev, q, pool, first, count) → Mel_Gpu_Future*`, and `mel_gpu_query_resolve_future_destroy(dev, f)` (frees the device-allocated result block + the future). The existing `mel_gpu_query_pool_resolve` (sync) is untouched — it is the sanctioned `*_sync` body.
- `src/vulkan/query.c`: the async op creates an engine-owned `MEL_GPU_MEMORY_READBACK` buffer, records `vkCmdCopyQueryPoolResults(64|WAIT)` + a `COPY_DEST→COMMON` buffer barrier into a standalone command list, submits via `mel_gpu_queue_submit` (so retirement rides the existing fence/poller spine — composition, MEL-ENGINE-IX), and chains a continuation that reads the mapped (HOST_COHERENT) readback, converts ticks × `period_ns` → host `u64` ns into the result block, resolves the result future, then tears down the command list + readback. Bad args / out-of-range fail loud and return an already-resolved ERROR future (MEL-ENGINE-VIII). The result block is allocated through `dev->alloc`, never `mel_malloc` (MEL-CODE-003).
- Future-gating: the result future is created before the submit, and the submit future is checked for already-resolved (no-pump / synchronous device) so the completion runs inline; otherwise the continuation is registered for pump delivery.
- Tests `vk_query.resolve_async_matches_sync` (async ns == sync ns, monotone) and `vk_query.resolve_async_bad_range_fails_loud`.

### Task 2 — async transfer upload (§7 queue roles / §3.3 retirement)
A transfer-queue completion-future upload, distinct from the synchronous immediate paths (`buffer_create(.data)`, `buffer_write`, `texture_write` all block on a fence inline today).

- New file `include/gpu/transfer.h` (purely additive, no shared-header collision with d3d12/metal): `Mel_Gpu_Transfer_Status`, `mel_gpu_buffer_upload_async(dev, q, dst, dst_offset, data, size)`, `mel_gpu_texture_upload_async(dev, q, dst, region, data, size)`, both returning a completion `Mel_Gpu_Future*`.
- New file `src/vulkan/transfer.c`: stages through an engine-created `MEL_GPU_MEMORY_UPLOAD` buffer, records the copy (buffer→buffer with offset, or buffer→image with the UNDEFINED→TRANSFER_DST→SHADER_READ transition pair) into a standalone command list on the requested queue, submits, and frees the staging buffer in the retirement continuation. Honors the §7 queue-role widening: `queue_request(TRANSFER)` lowers to the graphics family on the M1 single-queue backend, and the upload runs there.
- Tests `vk_transfer.buffer_upload_async_roundtrip` (upload → copy-back → byte-equal), `vk_transfer.buffer_upload_async_bad_params_fails_loud`, `vk_transfer.texture_upload_async_sampleable` (upload → copy-to-readback → byte-equal).

### Task 3 — shaderFloat64 request-and-grant (no silent default, MEL-CODE-007)
- `include/gpu/caps.h` (additive): `Mel_Gpu_Feature_Request.shader_fp64`.
- `src/vulkan/device.c`: `shaderFloat64` is enabled at `vkCreateDevice` only when `opt.features.shader_fp64 && avail.shaderFloat64`. `dev->caps.shader.fp64` is then **overwritten with the granted value** (was the adapter-availability value). Requested-but-unavailable logs a loud warn and reports not-granted. The cap now means "granted on this device", not "available on the adapter" — which is what a consumer branching on it needs.
- Test `vk_shader.fp64_request_and_grant`: no-request device reports `fp64 == false`; requesting device reports `fp64 == adapter availability`. **On MoltenVK 1.4.1 / Apple M3 Pro `shaderFloat64` is absent**, so this exercises the honest gate (request → not granted, not faked).

### Bug audit (coordinator hand-off, my owned tree)
- **H1 (MEL-CODE-007):** `caps.shader.fp16` / `caps.shader.int8` were probed-but-never-written (always false). Now read from the already-chained `VkPhysicalDeviceVulkan12Features.shaderFloat16` / `.shaderInt8`. MoltenVK grants both on this host (`fp16=1 int8=1`) — previously silently false.
- **H2 (silent capability lie):** `caps.memory.persistent_map` was hardcoded `true`. Now gated on a real host-visible `VkMemoryType` scan (`host_visible_any`); consumed at `device.c` to pick the persistent-map root-record lowering.
- **H3 (latent UAF):** `mel_gpu_sync_destroy` freed the semaphore immediately; now routes through `defer_free` like every other destroy. Added a `VkSemaphore semaphore` field to `Mel_Gpu_Deferred_Free` and its free path (`vk_backend.h`, `device.c`).
- New cap-consistency test `vk_caps.shader_and_memory_probes_written` (asserts `persistent_map`, nonzero heap bytes; logs the full probe truth).

### Queue-role honesty (folded into Task 2's §7 work)
- `caps.queues.dedicated_transfer` / `dedicated_compute` / `async_compute` were zero-initialized (silent default). Now probed from the queue-family bits in `caps.c` (transfer-only family → dedicated_transfer; compute-without-graphics → dedicated_compute + async_compute).
- `queue_request(.dedicated = true)` now **hard-fails loud** (§5.2 "hard-fails rather than promoting upward") instead of being ignored — the M1 backend wires only the graphics family, so a dedicated request cannot be honored truthfully and must refuse rather than return the graphics queue (MEL-ENGINE-VIII). Test `vk_queue.dedicated_request_gated_honestly` (both dedicated requests return NULL on Apple's single universal family; shared request succeeds).

## Kludges / debt (confessed, MEL-ENGINE-VIII)
- **Cross-thread command-pool free in the retirement continuation.** The async resolve/transfer continuations call `mel_gpu_command_list_destroy` (→ `vkFreeCommandBuffers` on the recording thread's per-thread pool). On the test device `pump == NULL`, so the continuation runs inline on the recording thread — correct. On a *threaded* reactor the pump poller runs on the reactor thread and would free from a foreign thread's pool, which Vulkan requires externally synchronized. Not exercised today; a fully threaded reactor needs the cleanup routed back to the recording thread or a device-owned transfer/resolve command pool. Same shape as the round-3 sync-resolve constraint.
- **Async paths tested only through the inline (no-pump) delivery.** The test device is created without a reactor, so `queue_submit` resolves the submit future inline and the continuation fires synchronously — this fully covers the resolve/upload/convert/cleanup logic but not pump-tick delivery ordering. Pump-driven delivery is already covered by `gpu-foundation`'s `future` tests (`manual_resolve_and_deliver`, `poller_resolves_on_tick`). I declined to spawn a threaded reactor in a test (flaky, no precedent in the gpu suites) in favor of determinism (MEL-CODE-005).
- **`Mel_Gpu_Query_Resolve.ns` ownership is caller-managed via a dedicated destroy.** `mel_gpu_future_value` returns a device-allocated block the caller must release with `mel_gpu_query_resolve_future_destroy` (not the plain `mel_gpu_future_destroy`, which would leak the block). This asymmetry is the price of a by-value result that outlives the future; documented at the header. The transfer futures carry no payload, so plain `mel_gpu_future_destroy` is correct for them.
- **`queue_request(.dedicated)` refuses unconditionally**, even on adapters whose caps now report a dedicated family exists. Honest (we cannot route to it yet) but conservative; wiring a second VkQueue at device-create is the real fix and is M-later work.

## Shared-header edits (additive only — flagged for orchestrator reconciliation)
- `include/gpu/caps.h` — `Mel_Gpu_Feature_Request.shader_fp64` (new bool, end of struct). d3d12/metal teams may grow the same struct; this is a pure append.
- `include/gpu/query.h` — added `#include <gpu/future.h>`, `Mel_Gpu_Queue` fwd-decl, `Mel_Gpu_Query_Resolve_Status`, `Mel_Gpu_Query_Resolve`, `mel_gpu_query_pool_resolve_async`, `mel_gpu_query_resolve_future_destroy`. The async-resolve signature is the spec's portable shape — d3d12/metal should converge on it.
- `include/gpu/transfer.h` — **new file**, owned by this round; no existing-header conflict.

## CLAUDE.md suggestions (recommendations only)
- Document the fresh-worktree nob bootstrap (`clang -std=c23 -g -Imodules/build -o nob nob.c`) and the gpu-vulkan test incantation (`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan`) — both live only in `modules/gpu/readme.md` and are non-obvious for a fresh agent.

## Suggestions
- A device-owned transfer/resolve command pool (one per device, internally synchronized) would retire the cross-thread-pool-free debt for every async upload/resolve path and unblock a threaded reactor.
- The d3d12/metal backends now have a concrete `query_pool_resolve_async` signature to mirror; the resolve-to-buffer pattern is mandatory on both (D3D12 needs the heap resolved to a buffer; WebGPU has no CPU getResults).
- `texture_upload_async` hard-codes the destination's post-upload state to `SHADER_READ_ONLY_OPTIMAL`; a future-returning `texture_write` per §6.2 should let the caller name the post-upload state (e.g. STORAGE for a compute-written texture) rather than assume sampling.
