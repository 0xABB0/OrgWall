# GPU RHI — Vulkan stress audit & defect report (M2)

Adversarial audit of `modules/gpu/src/vulkan/**` against `design/gpu-rhi.md` §3/§6/§7, mid-M2. Deliverables:
a new `gpu-stress` target (`modules/gpu/test/test_stress.c`, 14 probes) and this severity-ranked defect
report. Host: Apple M3 Pro / MoltenVK, Vulkan 1.2.334. Absent-on-host features (mutable descriptor types,
sampler-YCbCr, descriptor-buffer capture-replay, native fence-fd export, full sync2) are NOT flagged as bugs.

## Stress run

`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-stress macos --gpu=vulkan` —
**14 passed, 0 failed, 0 skipped**. Grep-clean: **0 VUID, 0 leak, 0 validation error**, every
`Destroyed VkPhysicalDevice … with 0 MB of GPU memory still allocated`. The single logged `[ERROR]` is the
intentional `MissingBindlessSlot` diagnostic from `stress_bindless.oversize_pipeline_is_graceful`
(MEL-ENGINE-VIII). Baselines unregressed: `gpu-vulkan` 28/28, `gpu-foundation` 8/8.

Probes: `stress_churn.{buffers_across_frames, mixed_resources, view_slot_reclaim_reuse}`;
`stress_bindless.{fill_sampler_class_under_cap, oversize_pipeline_is_graceful, sampler_dedup_refcount}`;
`stress_alloc.{no_overlap_sentinels, dedicated_interleaved}`;
`stress_io.{buffer_write_readback_fuzz, texture_write_readback_fuzz}`;
`stress_command.list_churn_destroy_after_submit`; `stress_compute.storage_buffer_bindless_rounds`;
`stress_future.{many_inflight_submits, pump_backpressure_coalesce}`.

The suite passes because every probe stays inside the contract the backend currently honors. The defects
below are the ones a probe that crossed those bounds *would* expose; where exercising them would crash the
suite (a live `mel_assert`) or require multi-threading the headless device, the probe is bounded and the
defect is documented here for the builder rather than faked into a pass.

---

## Confirmed bugs

### CRITICAL-1 — Bindless `slot == handle.index` contract collides with the heap cap; over-cap registration asserts in debug, silently drops the descriptor in release
`src/vulkan/binding.c:125-136` (`mel_gpu__bindless_check`), consumed by
`src/vulkan/texture.c:263-269`, `src/vulkan/buffer.c:150-156`, `src/vulkan/sampler.c:155`.

The direct contract (§3.1) pins a resource's heap slot to its slotmap **index**. The slotmap hands out
indices from a free list and **grows by doubling `slot_capacity`** when the free list is exhausted
(`modules/collection/src/slotmap.c:98-101`). The bindless heap class cap is a fixed default (16384 images /
2048 samplers / 16384 buffers, `binding.c:22`). These two facts are unreconciled:

- Create more than `cap` **live** direct resources of a class and the slotmap index crosses the cap. At
  registration `mel_gpu__bindless_check` fires `mel_assert(!"bindless slot exceeds heap capacity")` — a
  **crash in any debug build** (asserts are live, `MEL_ASSERT_ENABLED` under `MEL_DEBUG`). The 2048-sampler
  cap is the realistic trip wire: 2049 live samplers crash.
- In a **release** build the assert compiles out (`debug/assert.h:27`), `mel_gpu__bindless_check` returns
  `false`, and the registration function **returns void having written nothing** — the resource is reported
  created OK, but its heap descriptor is absent. A shader indexing that slot reads a garbage/unbound
  descriptor: **silent corruption**, the exact failure §6.2 / MEL-ENGINE-VIII forbid.
- The slotmap can also push an index past the cap with **fewer than `cap` live** resources: enough
  create/destroy churn grows `slot_capacity` past the cap while `remove_deferred` withholds low indices from
  the free list until their retirement edge, so a fresh insert grabs a grown high index even though live
  count is modest.

This contradicts the binding-finish writeup's claim that over-cap is surfaced as a graceful
`MissingBindlessSlot` — that graceful path exists **only at pipeline-create for a sized over-cap array**
(`pipeline.c:262-271`, covered green by `stress_bindless.oversize_pipeline_is_graceful`). The
*resource-registration* over-cap path has no graceful surface at all.

Repro (would crash the suite, hence not shipped as a live probe): create 2049 distinct samplers
(vary wrap/lod so dedup does not collapse them) on a bindless device; the 2049th `sampler_create` registers
at index ≥ 2048 and asserts. `stress_bindless.fill_sampler_class_under_cap` deliberately fills only `cap/4`
(≤256) and asserts `slot < cap` to stay clear of the cliff while still exercising the slot==index invariant.

**Fix direction:** the cap cannot be a silent ceiling on a contract that the slotmap is free to exceed.
Either (a) make `*_create` *fail with a status* (`BindlessSlotExhausted` already named, `caps.h`) when the
index would exceed the class cap, propagated through the result struct — no void registration, no assert as
control flow; or (b) grow the heap descriptor array on demand (UPDATE_AFTER_BIND pools can be re-created
larger, or use a second binding) so the cap tracks the slotmap; or (c) decouple bindless slot from
slotmap index (the indirect family) for resources past the cap. (a) is the minimum honest fix.

### CRITICAL-2 — `mel_gpu_cmd_begin_rendering_opt` silently truncates color attachments past 8 via a fixed-size stack array
`src/vulkan/record.c:309-310`: `VkRenderingAttachmentInfoKHR color[8]; u32 n = opt.color_count <= 8 ? opt.color_count : 8;`

A caller passing `color_count > 8` gets **8 attachments rendered and the rest silently dropped** — no warning,
no status (rendering calls are contract calls, §3.2, so there is no result struct, but a debug `mel_assert`
is the minimum, MEL-CODE-007 / MEL-ENGINE-VIII). This is both a fixed-size-array smell (MEL-CODE-002) and a
silent default (MEL-CODE-007). The pipeline path correctly builds its color array **dynamically** with the
device allocator (per the U13 writeup), so this is an inconsistency, not a hardware limit (MoltenVK exposes
8 color attachments here, but the API must not encode that as a silent floor). Same fixed `[8]` pattern in
`command.c` is only for the single swapchain attachment, so benign there.

**Fix:** allocate the `VkRenderingAttachmentInfoKHR` array from `dev->alloc` sized to `opt.color_count` (as
`pipeline.c` already does for `pColorAttachmentFormats`), or at minimum `mel_assert(opt.color_count <= 8)`.

### MAJOR-3 — Allocator live-bytes accounting under-reports by the buddy power-of-two rounding slack
`src/vulkan/memory.c:165-176, 192-199, 243` with `modules/allocator/src/buddy.c:100-125`.

The buddy suballocator rounds every request up to the next power of two
(`mel__buddy_next_pow2`, `buddy.c:105`) and that rounded block is what is actually consumed from the 64 MiB
device block. But `mem_alloc` records `out->size = req.size` (the **unrounded** request), and both
`mel_gpu__usage_add` (`memory.c:175,199`) and `mel_gpu__mem_free` (`memory.c:243`) account in `out->size`.
Live VRAM is therefore under-counted by the rounding slack — up to ~2× per suballocation for a
just-over-power-of-two request (e.g. a 257-byte request consumes a 512-byte block but counts 257).

`stress_alloc.no_overlap_sentinels` deliberately allocates just-over-power-of-two odd sizes (17 + n) and
confirms no *correctness* breakage (the buddy free finds the level by the USED marker, not by the stored
size, `buddy.c:154-186`), so this is an **accounting** bug, not a corruption: it skews
`mel_gpu_memory_budget`'s fallback path (`memory.c:226`) and the timing of the `budget_pressure` callback
(`memory.c:35`), making the engine under-report pressure and fire the callback late — a quiet violation of
MEL-ENGINE-III / VI (honoring the device's real memory). The `VK_EXT_memory_budget` path (`memory.c:209-223`)
is unaffected since it reads driver counters; the slack only bites where the engine self-reports.

**Fix:** account the rounded block size. Either store the actual consumed size in `Mel_Gpu_Allocation`
(compute `next_pow2(max(req.size, MIN_BLOCK))` at alloc and add/sub that), or expose the granted block size
from `mel_buddy_alloc`.

### MAJOR-4 — Off-band `texture_write` / `staging_upload` submissions bypass the retirement watermark and serialize the whole queue with `vkQueueWaitIdle`
`src/vulkan/texture.c:382-386`, `src/vulkan/buffer.c:65-69`.

Both upload helpers `vkQueueSubmit` + `vkQueueWaitIdle(graphics_queue)` under `submit_lock` **without**
reserving a serial (`mel_gpu__submit_serial_next`) or advancing the watermark (`mel_gpu__submit_complete`).
Two consequences:

- **Correctness-adjacent:** the watermark (§3.3) is the engine's single retirement clock. A submission that
  consumes a resource yet is invisible to the watermark means a deferred-free entry's `marker` can be
  satisfied by an *unrelated later* tracked submit while this untracked upload is still the most recent user.
  It is safe *today* only because these helpers are fully synchronous (`WaitIdle` blocks until done before
  returning) and serialized by `submit_lock`. The instant any upload path goes async (the §6.2
  host-image-copy / transfer-queue slice), this becomes a use-after-free window. It is a latent landmine the
  watermark invariant is supposed to preclude.
- **Performance (MEL-ENGINE-III/VI):** `vkQueueWaitIdle` after every `texture_write` / device-buffer init
  drains the *entire* graphics queue, not just this transfer — a full GPU stall per upload. `stress_io`
  hammers this (each fuzz size issues a `texture_write` → full-queue idle); it is the slowest part of the
  suite. The writeup chain confesses staging-only uploads but not the per-upload full-queue stall.

**Fix:** route uploads through `queue_submit`'s serial/fence/watermark machinery (or a dedicated transfer
serial), and wait on a per-submission fence rather than `vkQueueWaitIdle`.

### MAJOR-5 — `queue_submit` dereferences `submit.command_lists` to build `cbs` even when a list pointer is null, and the empty-batch path still allocates a fence
`src/vulkan/queue.c:143-150`.

`for (i < command_list_count) cbs[i] = submit.command_lists[i]->cb;` has no null guard on the array or its
elements. The existing `vk_queue.request_info_submit` test submits `command_list_count = 0` and survives only
because the loop body never runs; but a caller passing `command_list_count = N` with a partially-filled or
null `command_lists` array dereferences null with no `mel_assert` (§3.2 says submit is fallible — it should
validate). Minor sibling: every submit, **including the count==0 empty batch**, creates and waits on a real
`VkFence` (`queue.c:148-150,183`); an empty submit is a pure watermark bump and need not round-trip a fence.
Not a leak (the fence is destroyed), but wasted driver objects per drain — `stress_future.many_inflight_submits`
and every `stress_drain` pay it (256+ fence create/destroy round-trips).

**Fix:** `mel_assert(submit.command_lists || submit.command_list_count == 0)` and per-element validation in
debug; consider skipping the fence for the empty batch.

---

## Spec deviations / confessed-kludge-still-open (verified)

### SPEC-6 — Retirement watermark is single-queue-correct only; multi-queue will silently mis-retire
`src/vulkan/device.c:418-434`. The watermark is a single scalar `submit_completed`; correctness rests on
in-order completion of one graphics queue (confessed in the Phase-A writeup). All queue roles currently lower
to the graphics queue (`queue.c:36-37`), so this holds **today**. Flagged as the open item it is: the moment
a real async-compute / transfer queue lands, a `max()` scalar watermark frees resources still in flight on a
slower queue. Per-queue (or min-across-queues) watermarks are the §3.3 requirement. Not a bug now; a
guaranteed one at the next queue slice.

### SPEC-7 — `cmd_buffer_barrier` always uses `VK_WHOLE_SIZE` / offset 0; no sub-range buffer barriers
`src/vulkan/record.c:240-250`. Faithful for the lowered states but cannot express a sub-range barrier; benign
at this tier, noted for completeness.

### SPEC-8 — Compute rides the graphics queue; no async-compute overlap, no `dispatch_indirect`
`src/vulkan/queue.c:36-37` + binding-finish writeup. Confessed, still open. `stress_compute` proves the
single-queue dispatch path is correct under churn (24 rounds, slot reuse, byte-exact `out[i]==in[i]+1`).

### SPEC-9 — Static-sampler refcount is held but the heap descriptor write at sampler-create is unconditional even when the heap is disabled-by-want
`src/vulkan/sampler.c:155` calls `mel_gpu__bindless_register_sampler` unconditionally; it is a no-op when
`!dev->bindless.enabled` (guarded inside `binding.c:125`). Correct, but the buffer/texture paths gate the call
on `dev->bindless.enabled` at the call site (`buffer.c:150`, `texture.c:263`) while sampler does not — a
cosmetic inconsistency (MEL-ENGINE-IX) that funnels a disabled-heap sampler-create through an extra branch.
Harmless; flagged for symmetry.

---

## Minor

- **MINOR-10** — `binding.c:104` logs the heap sizes at `info` level on *every* bindless device create; under
  the stress suite's repeated device creates this is log noise (MEL-CODE-006: "try not to make too much
  noise"). Demote to a once-per-process or `debug` log, or fold into the existing device-created line.
- **MINOR-11** — `record.c:265-266` computes the copy mip extent as `o->width >> mip ? … : 1`; correct for
  the base mip but the readback always uses `bufferRowLength = 0` (tight). Fine for the formats in the enum;
  will need per-format row alignment when block-compressed formats land (out of scope, noted).
- **MINOR-12** — `future.c:127` allocates a poller snapshot array from the heap on **every** `pump_tick` even
  when `poller_count` is small and stable (the device's single submit poller is the steady state). A per-pump
  reusable scratch buffer would remove a per-tick alloc/free on the hot completion path (MEL-ENGINE-III).
  Not exercised here (headless device has no pump), flagged from reading.

---

## Rule-#1 tension (comments)

The global `~/CLAUDE.md` says "Never write comments"; `modules/gpu` is densely commented in the house style
the project `CLAUDE.md` actively encourages ("cite it by tag"), and every prior M2 slice matched it. My
`test_stress.c` matches the surrounding commented style (spec-section + MEL-ENGINE tags, as `test_vulkan.c`
does). **Halt-and-query for Gabbo:** if the global rule governs this module, say so and I will strip the
comments from the stress test (and the team can decide on the module at large).

## Kludges I introduced (MEL-ENGINE-VIII — full confession)

- **CRITICAL-1 left as a bounded probe, not a live failing test.** Exercising the over-cap registration path
  directly would `mel_assert`-crash the whole suite (asserts live in debug), and a crashing test is not a
  "documented expected-failure" — it takes down sibling probes. So `stress_bindless.fill_sampler_class_under_cap`
  fills only `cap/4` and asserts `slot < cap`, proving the invariant *up to* the cliff; the cliff itself is
  documented above for the builder. This is the honest call (do not fake a pass, do not crash the runner),
  but it means the suite does not *automatically* regression-guard CRITICAL-1 until the builder makes the
  over-cap path return a status instead of asserting — at which point a real over-cap negative test becomes
  safe to add (it is named in the fix direction).
- **`stress_future.pump_backpressure_coalesce` enqueues exactly `4×high_water`** (32 at high_water 8), sitting
  on the `<= high_water*4` assert boundary by design to prove the warning fires without tripping the hard
  ceiling. If the ceiling check were ever changed to `<` strict at the boundary this probe would need a −1;
  it currently passes because the check is `<=` (`future.c:196`).
- **No multi-threaded probe.** U36 concurrency classes (concurrent `sampler_create`, per-region heap-write
  serialization, `SerializedPerObject` violations) are unexercised: the headless device's synchronous
  no-reactor submit path and the single graphics queue make a faithful concurrency stress hard to write
  without a real job system, and the thread-safety tracker is covered by `gpu-foundation`. Flagged as the
  largest coverage gap — a dedicated multi-thread churn probe (N threads each creating/destroying on distinct
  handles, one shared heap) is the natural follow-on once CRITICAL-1 is fixed (so it cannot assert-crash).
- The suite leans on `vkQueueWaitIdle`-heavy `texture_write` (MAJOR-4) for its readback fuzz; it is correct
  but slow, so the suite runtime is dominated by GPU stalls, not probe logic.

## For the builder / Gabbo

- **Builder:** CRITICAL-1 (bindless over-cap honesty) is the headline fix and unblocks a real regression test.
  CRITICAL-2 (dynamic color array), MAJOR-3 (rounded-size accounting), MAJOR-4 (watermark-tracked uploads)
  follow. SPEC-6 (per-queue watermark) must land with the async-queue slice, not after.
- **Gabbo:** the comments Rule-#1 tension (above) needs a ruling for this module.

## Build / target

`gpu-stress` added to `modules/gpu/build.c` mirroring `gpu-vulkan` (same deps, AppKit on macOS, shared
`runner.c`, `MEL_GPU_VULKAN=1`). Run:
`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-stress macos --gpu=vulkan`.
