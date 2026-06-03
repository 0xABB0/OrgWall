# GPU RHI — BUG-FINDINGS audit, round 4 (read-only)

Adversarial read-only audit of `modules/gpu/` (include + src/{vulkan,d3d12} + src/*.c + test/) and
`apps/hello-gpu/` against `design/gpu-rhi.md` §3.1/§3.2/§3.3/§3.7, MEL-ENGINE-VIII, and
MEL-CODE-001/002/003/007. Host: Apple M3 Pro / MoltenVK 1.4.1 (Vulkan 1.2.334), validation on.
The only file written this round is this writeup. No source/header/test changed.

## Suites run (observe-only, NOFORK, `--gpu=vulkan`)

- `gpu-foundation` **8/8**, `gpu-concurrency` **10/10**, `gpu-stress` **20/20**, `gpu-vulkan` **40/40**.
- Zero `leak:`, zero VUID, zero validation error observed; every device-teardown reports
  `Destroyed VkPhysicalDevice … with 0 MB of GPU memory still allocated`. The round-3 zero-leak claim
  reproduces. (`gpu-bench`/`gpu-visual` not re-run; not on the critical path for the contracts audited.)

The findings below are static-analysis defects the green suites do not exercise (most are latent: they
fire only once M1's omitted wait/signal wiring or the indirect-handle family lands, or only on hardware/
code paths the headless single-queue MoltenVK harness never reaches).

---

## CRITICAL

None. No live memory-corruption, double-free, or use-after-free is reachable through the current public
API on the tested path. The most dangerous items (sync-destroy UAF, texture slot-reuse) are latent — see
HIGH — because the feature that would trigger them (submit wait/signal arrays) is itself unimplemented.

## HIGH

### H1 — `caps.shader.fp16` is probed-but-never-written on Vulkan (silent default; round-3 sibling)
- **file:** `modules/gpu/src/vulkan/caps.c:71-73` (writes int16/int64/fp64, never fp16).
- **contract:** MEL-CODE-007 (no silent defaults); MEL-ENGINE-VIII. This is the exact bug class the
  round-3 writeup caught for `queries.timestamp_compute_and_graphics` — a queryable fact left at the
  struct's `{0}` zero-init.
- **evidence:** `caps.c` chains `VkPhysicalDeviceVulkan12Features feat12` (line 67) and reads it for
  `timeline` (line 78), but never reads `feat12.shaderFloat16`, so `out->shader.fp16` stays `false`.
  The D3D12 backend *does* probe it (`d3d12/caps.c:71` `out->shader.fp16 = o4.Native16BitShaderOpsSupported`),
  proving the field is meant to be populated. The spec §6.4.1 forbids silent precision demotion: "If a
  shader asks for `fp16` … and the backend cannot honor it, creation fails." With `fp16` always false on
  Vulkan, fp16-capable devices (most desktop/Apple Silicon) are reported as fp16-incapable, so a U13
  pipeline-create capability gate keyed on `caps.shader.fp16` would wrongly refuse a valid shader.
- **trigger:** query `mel_gpu_device_caps(dev)->shader.fp16` on any Vulkan device that supports
  `shaderFloat16` (M-series via MoltenVK reports it). Returns `false`.
- **fix sketch:** add `VkPhysicalDeviceVulkan12Features` read of `shaderFloat16` (and `shaderInt8`, see
  M1) into `out->shader.fp16` in `mel_gpu__caps_probe`, mirroring `device.c`'s pattern of refining caps
  from the already-queried feature chain.

### H2 — `caps.memory.persistent_map` hardcoded `true` regardless of host-visible memory presence
- **file:** `modules/gpu/src/vulkan/caps.c:105` (`out->memory.persistent_map = true;` unconditional);
  `modules/gpu/src/d3d12/caps.c:35` (same hardcode).
- **contract:** MEL-CODE-007; §6.1 (`buffer_persistent_ptr` is UPLOAD/READBACK only, "native zero-cost
  live pointer"); §6.7 lowering reads this flag.
- **evidence:** the value never reflects whether a host-visible memory type actually exists; `caps.c`
  computes `host_visible_device_local` and `host_visible_bytes` from `vkGetPhysicalDeviceMemoryProperties`
  right above (lines 81-103) but does not condition `persistent_map` on any of it. It is then consumed in
  `device.c:255` to pick `root_record_update = persistent_map ? PERSISTENT_MAP : STAGING_COPY` and would be
  read by `buffer_persistent_ptr`. A device with no `HOST_VISIBLE` heap (a pure-device-local config, or a
  driver where mapping is unavailable) would still advertise `persistent_map = true` and select the
  persistent-map root-record lowering — a silent capability lie (MEL-ENGINE-VIII).
- **trigger:** not reachable on the M3/MoltenVK harness (it has host-visible memory), hence green suites.
  Surfaces on any adapter lacking a host-visible heap.
- **fix sketch:** set `persistent_map` from the actual presence of a `HOST_VISIBLE` memory type
  (`host_visible_bytes > 0` or the loop that already scans `mem.memoryTypes`).

### H3 — `mel_gpu_sync_destroy` frees the semaphore immediately, not future-gated (latent UAF)
- **file:** `modules/gpu/src/vulkan/sync.c:53-55` — `table_remove` (immediate) + `vkDestroySemaphore`
  (immediate), with no `defer_free`/`table_remove_deferred`.
- **contract:** §3.3 "Retirement is future-gated … deferred-destroy of resource slots (U1) … takes a
  dependency on the completion future of the last submission consuming it." §3.7 destroy is
  `SerializedPerObject`. Every *other* destroy path defers: `buffer.c:201-202`,
  `texture.c:319-320`, `sampler.c:195-198` use `table_remove_deferred` + `defer_free`. Sync is the lone
  non-deferred destroy.
- **evidence:** a semaphore handed to `queue_submit` as a wait/signal is consumed by an in-flight GPU
  submission; destroying it under the GPU is the classic "VkSemaphore destroyed while in use" UAF.
- **why latent, not Critical:** `Mel_Gpu_Submit` (`include/gpu/queue.h:56-60`) carries **only**
  `command_lists[]` — it has **no `wait[]`/`signal[]`** at all, and `queue.c:172-176` builds a
  `VkSubmitInfo` with zero wait/signal semaphores. So user-created `Mel_Gpu_Sync` objects are never
  actually submitted today; the immediate destroy cannot corrupt because nothing references the semaphore.
  The bug is armed the moment wait/signal wiring lands (see M2).
- **fix sketch:** when wait/signal arrives, route `sync_destroy` through `table_remove_deferred` +
  `defer_free(.sync_semaphore = sem)` (extend `Mel_Gpu_Deferred_Free` + `mel_gpu__free_deferred_entry`),
  same shape as buffer/view/sampler.

## MEDIUM

### M1 — Further probed-but-never-written caps (silent-default cohort)
- **file:** `modules/gpu/src/vulkan/caps.c` and `modules/gpu/src/d3d12/caps.c`.
- **contract:** MEL-CODE-007.
- **evidence (grep of `\.<field> =` across `modules/gpu/src/`):** never assigned on *either* backend —
  `shader.int8`, `queues.async_compute`, `queues.dedicated_transfer`, `queues.dedicated_compute`,
  `presentation.vrr`, `presentation.frame_latency_waitable`, `presentation.shared_presentable_image`,
  `presentation.pre_rotation`, `memory.sparse_buffer`, `memory.sparse_texture`. All silently read back as
  `false`/`none`. Of these, `shader.int8` (Vulkan `shaderInt8`), `async_compute`/`dedicated_*` (derivable
  from `vkGetPhysicalDeviceQueueFamilyProperties` — note the backend only ever enumerates a graphics
  family at `device.c:10-28`), and the presentation flags are real, queryable facts being reported false.
  `sparse_*` is defensibly "feature not implemented → false," but per MEL-CODE-007 even that should be a
  deliberate write, not a zero-init fall-through, so the next reader can tell "probed and absent" from
  "never wired." This is the same hazard that hid the round-3 timestamp bug for two rounds.
- **fix sketch:** populate each from its probe (or, for genuinely-unimplemented tiers, assign the `none`
  value explicitly with a deliberate statement) so no cap relies on struct zero-init.

### M2 — `queue_submit` silently drops semaphore synchronization (spec-omitted surface)
- **file:** `modules/gpu/include/gpu/queue.h:56-60` (`Mel_Gpu_Submit` lacks `wait[]`/`signal[]`);
  `modules/gpu/src/vulkan/queue.c:172-176` (no `pWaitSemaphores`/`pSignalSemaphores`).
- **contract:** §5.2 "`queue_submit(queue, { command_lists[], wait[], signal[] })`. The `wait` and `signal`
  arrays are U17 timeline or binary semaphores." §3.1's whole `Mel_Gpu_Sync` type exists to be submitted.
- **evidence:** `sync_create`/`sync_destroy` exist and work, but there is no path to *use* a sync object
  at submission — async-compute overlap, cross-queue timeline waits, and U5 imported-semaphore interop are
  all unreachable. This is an M1 scope gap (acknowledged by the single-queue backend), recorded so the
  orchestrator routes H3's deferred-destroy fix together with the wait/signal wiring.
- **fix sketch:** add `wait[]`/`signal[]` (sync handle + stage/value) to `Mel_Gpu_Submit`; resolve to
  `VkSemaphoreSubmitInfo` arrays; land H3's deferral in the same change.

### M3 — `texture_destroy` uses immediate `table_remove`, breaking the deferred-reclaim symmetry
- **file:** `modules/gpu/src/vulkan/texture.c:185` (`mel_gpu__table_remove`, immediate) vs the deferred
  `table_remove_deferred` used by `texture_view_destroy` (319), `buffer_destroy` (201), `sampler_destroy`.
- **contract:** §3.7 "deferred-free runs on the U3 completion pump and does not race with subsequent
  creation at the same slot index"; §3.3 future-gated retire.
- **evidence:** the texture's `VkImage`+memory free *is* deferred (line 187), but the slotmap slot is
  reclaimed immediately, so the index becomes available for reuse before the consuming submission retires.
  Generation roll keeps stale handles loud (`alive()` false), and textures carry no bindless slot (only
  views do, via the deferred path), so no descriptor aliasing — hence benign today. But it is an
  inconsistent retirement contract and a slot-reuse hazard the instant any per-texture deferred state is
  added.
- **fix sketch:** switch `texture_destroy` to `table_remove_deferred` + `.has_reclaim` reclaim of the
  texture table, matching buffer/view/sampler.

### M4 — Imports return the *direct* handle family; indirect family is unimplemented
- **file:** `modules/gpu/src/vulkan/buffer.c:229-240` (`mel_gpu_buffer_import` returns `Mel_Gpu_Buffer`,
  the direct type, with `ownership = BORROWED`); `include/gpu/handle.h` has the `MEL_GPU_HANDLE_INDIRECT`
  macro and `sampler.h:10` declares `Mel_Gpu_Sampler_Indirect`, but no `Mel_Gpu_Buffer_Indirect` /
  `_Texture_View_Indirect` / `_Accel_Struct_Indirect` exist and nothing returns them.
- **contract:** §3.1/§3.5 "Imported … resources use *indirect* peer types … carry the slot field
  separately … The two families do not implicitly convert." §3.1's whole point is that an import cannot
  honor `slot == handle.index`, so it must not masquerade as a direct handle.
- **evidence:** an imported buffer returned as a direct `Mel_Gpu_Buffer` would, if registered in bindless,
  assert `bindless_slot == handle.index` (the direct contract) — which the import cannot guarantee. Today
  `buffer_import` never registers it in the heap, so it does not corrupt; the type-safety contract is the
  casualty (a borrowed import is type-indistinguishable from an engine-owned buffer at a bindless site).
- **fix sketch:** declare the missing indirect types; have `*_import` return them; have `*_bindless_slot`
  on the indirect type read the stored slot. M1-scoped; record for the type-system pass.

## LOW

### L1 — `exts[8]` and `adapters[16]` fixed-size arrays (MEL-CODE-002)
- **file:** `device.c:71` `const char* exts[8];` (6 conditional pushes, no bound check — currently safe at
  6 < 8 but adds no slack for the next extension); `device.c:551` `Mel_Gpu_Adapter* adapters[16];` followed
  by a dead `if (n > 16) n = 16;` (line 555) — `mel_gpu_adapters(inst, adapters, 16)` already caps the
  fill at 16, so the guard is unreachable and the array silently truncates a 17th adapter.
- **contract:** MEL-CODE-002 (never fixed `[N]` arrays). No live overflow; flagged as the rule violation
  and a latent truncation.
- **fix sketch:** size `exts` from a small dynamic array or bump-and-assert; enumerate adapter count first
  and allocate, or assert `n <= 16` instead of the dead clamp.

### L2 — Swapchain silently ignores the requested format with no warning channel
- **file:** `modules/gpu/src/vulkan/swapchain.c:42-82` (`mel_gpu__choose_format`): falls back to
  `formats[0]` (line 50-51) / first sRGB-nonlinear / BGRA8 without surfacing that the requested format was
  not honored; `swapchain_create` returns a bare `Mel_Gpu_Swapchain*` with no status, so it *cannot* carry
  the §3.2 success-with-degradation warning the spec mandates for substitutions.
- **contract:** §3.2 "request … none available, get … plus a warning naming the substitution"; MEL-CODE-007.
- **fix sketch:** give swapchain creation the `{value,status}` result shape and emit a `format_substituted`
  warning bit when the granted format differs from the request.

### L3 — Pump backpressure warning is one-shot and uses `mel_log_warn`, not a recurring device-event U2 warning
- **file:** `modules/gpu/src/future.c:191-195` — `if (depth > high_water && !pump->warned)` sets
  `pump->warned = true` permanently (never reset), and logs via `mel_log_warn` rather than surfacing a U2
  warning "on the next device-level event."
- **contract:** §3.3 "a U2 warning surfaces on the next device-level event" (implies recurring/observable
  through the status channel, not a once-ever log line). The 4× hard-ceiling assert (`future.c:196`) and
  same-future coalescing (`mel_gpu_future_resolve`'s `atomic_exchange(&f->claimed,1)`, line 207) are both
  correctly implemented — only the warning surfacing deviates.
- **fix sketch:** surface the backpressure warning through a device-level status/event rather than a
  latched log; reset the latch once `depth` drops back under the mark.

---

## §3.7 tracker: known deviation + false-premise test

- **Tracker reports, does not assert (deliberate, sanctioned, but a spec deviation worth restating).**
  §3.7 says "Misuse in debug is a `mel_assert` from the thread-safety tracker." The implementation
  (`threading.c:70-74`) `mel_log_error`s and returns — never asserts. This is round-2 BUG-2's
  report-not-abort decision (so the runner survives genuine cross-thread misuse under NOFORK), not a
  regression; recorded only because the literal §3.7 wording still says `mel_assert`. The wiring is real
  and end-to-end-proven (`buffer.c:247`, `record.c:33/47`, `queue.c:144`, all destroy paths).

- **False-premise test still present.** `conc_tracker.device_accepts_flag_but_tracker_is_unwired`
  (`modules/gpu/test/test_concurrency.c:716-735`) asserts the tracker is *unwired*; BUG-2 wired it, so the
  name and premise are false. The body only creates+destroys one buffer single-threaded, so it passes
  vacuously (no cross-thread misuse → no report) and is not a behavioral lie — but the name misleads.
  Round-3 recommended rename to `…tracker_is_wired_single_thread_clean` or deletion; still outstanding.

## Categories with nothing to report

- **Double-free / leak on tested paths:** none found. Destroy paths copy-out under `obj_lock`, defer the
  Vulkan free, and `mel_gpu__free_deferred_entry` guards each member; device-teardown leak reporter
  (`device.c:268-307`) fires zero across all four suites. The buddy allocator's usage accounting balances
  (dedicated stamps `req.size`/frees `a->size`; suballocated stamps/frees `consumed`).
- **Slot generation-reuse race:** the deferred-reclaim ledger (`device.c:437-516`) gates slot reclaim on
  the submit-serial watermark; the round-3 same-slot-reuse probes are green. M3 is the one residual
  asymmetry (texture immediate-remove), latent only.
- **Status model (§3.2):** correct — per-action enums with low-2-bits severity (`status.h`), branch-free
  `failed/warned/ok`, handle validity as the usability signal.
- **`*_sync` reactor-thread debug-assert (§3.3):** honored — `mel_gpu_future_wait` (`future.c:233`) asserts
  `!mel_reactor_is_owner` of the pump's reactor.
- **MEL-CODE-003 (allocator misuse):** clean — zero `mel_malloc` in `modules/gpu/src/`; everything threads
  an allocator or uses `mel_alloc_heap()` deliberately.
- **MEL-CODE-001 (enums-as-closed-sets):** the cap *tiers* and roles are protocol-shaped graduated enums
  the spec itself prescribes (§3.4), so they are the sanctioned enum case, not violations.

## Severity tally

Critical 0 · High 3 (H1 fp16 silent default, H2 persistent_map lie, H3 latent sync-destroy UAF) ·
Medium 4 (M1 cap cohort, M2 submit wait/signal omission, M3 texture immediate-remove, M4 indirect family) ·
Low 3 (L1 fixed arrays, L2 swapchain format-substitution warning, L3 pump warning surfacing).

The single most important finding is **H1**: `caps.shader.fp16` is the exact probed-but-never-written
silent-default class the round-3 writeup flagged for `timestamp_compute_and_graphics`, still live on the
Vulkan backend (and accompanied by the M1 cohort), and it actively misreports a feature most target
devices support — directly feeding the §6.4.1 "no silent precision demotion" gate the wrong answer.
