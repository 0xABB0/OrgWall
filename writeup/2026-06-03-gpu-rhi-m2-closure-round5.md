# GPU RHI — M2-closure round 5 (Vulkan submit-sync + audit cohort)

Round-5 pass off round-4 `main` (HEAD `f68233e`), on branch `gpu-rhi-m2-closure-round5`.
Goal: close the substantive M2 software gaps the round-4 audit left open, leading with the
`queue_submit` wait/signal surface (§5.2) — the one real M2 deliverable still missing — and
sweeping the cheap silent-default / cleanup cohort. D3D12 round-4 remains UNVERIFIED on Windows
(win-pilot offline, port-22 timeout reconfirmed this session); that verification is unblockable
from here and was not the lane.

## Work done & why

- **§5.2 `queue_submit` wait/signal (Vulkan).** `Mel_Gpu_Submit` gains `wait[]`/`wait_count`
  and `signal[]`/`signal_count` arrays of `Mel_Gpu_Submit_Sync { Mel_Gpu_Sync sync; u64 value; }`
  (`include/gpu/queue.h`). Purely additive — every call site brace-initializes, so none broke.
  `vulkan/queue.c` resolves each handle to its `VkSemaphore`, builds `VkSubmitInfo` with
  wait/signal arrays, and chains `VkTimelineSemaphoreSubmitInfo` (core 1.2) only when a timeline
  semaphore is present in the batch. Wait dst-stage is `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT`
  (conservative-correct; a per-entry stage field can be added additively later without breaking
  the call site). Invalid sync handle → log + debug-assert + error future (submit is fallible,
  §3.2). This unblocks async-compute overlap, cross-queue timeline waits, and the U5
  imported-semaphore path the spec's whole `Mel_Gpu_Sync` type exists to feed.

- **Future-gated reclaim completed for sync (audit H3) and texture (audit M3).** Both
  `sync_destroy` and `texture_destroy` now route the owned path through `table_remove_deferred`
  + `defer_free(..., .reclaim_table, .reclaim_index, .has_reclaim = true)`, mirroring
  `buffer_destroy`. Round 4 had already deferred the `VkSemaphore` / `VkImage` free; this defers
  the **slot reclaim** too, so the index cannot be reused before the consuming submission retires
  (§3.3). H3 was latent until this round wired wait/signal — it is now armed *and* covered.

- **Caps silent-default remainder (audit M1, MEL-CODE-007).** Round 4 already wired `fp16`,
  `int8`, `async_compute`, `dedicated_{compute,transfer}` on Vulkan. Remaining fields now written
  deliberately rather than left to `{0}` fall-through:
  - Vulkan: `memory.sparse_buffer`/`sparse_texture` from base `sparseBinding` +
    `sparseResidency{Buffer,Image2D}`; `presentation.shared_presentable_image` from the
    `VK_KHR_shared_presentable_image` extension probe; `presentation.{vrr,frame_latency_waitable,
    pre_rotation}` set to explicit `false` (not probeable at device-create without a surface on
    this path).
  - D3D12 (uncompiled — see debt): deliberate `queues.{async_compute,dedicated_*}=true`
    (universal on D3D12), `shader.int8=false`, sparse from `TiledResourcesTier >= TIER_1`,
    presentation flags explicit `false`.

- **L1 — fixed arrays (MEL-CODE-002).** `mel_gpu__pick_adapter` dropped its `Mel_Gpu_Adapter*[16]`
  + dead-but-load-bearing `n>16` clamp; it now iterates `inst->adapters[0..adapter_count]`
  directly (no copy, no truncation of a 17th adapter). `device_create`'s `const char* exts[8]`
  became a grow-on-demand array via a new `mel_gpu__ext_push` helper (allocator-threaded,
  MEL-CODE-003), freed right after `vkCreateDevice` on both paths.

- **L2 — swapchain format substitution.** `mel_gpu__choose_format` now `mel_log_warn`s when the
  requested format is unavailable and a substitute is chosen — removing the silent swap
  (MEL-CODE-007). The full §3.2 `{value,status}` reshape of `swapchain_create` is **not** done
  (broad blast radius across ~20 hello-gpu screens + tests); deferred, see suggestions.

- **L3 — pump backpressure latch.** `future.c` resets `pump->warned` once `ready_count` drops
  back under the high-water mark, so the backpressure warning recurs instead of firing once-ever
  (§3.3 "surfaces on the next device-level event"). Still a `mel_log_warn`, not a device-event
  status channel — see debt.

- **False-premise test renamed.** `conc_tracker.device_accepts_flag_but_tracker_is_unwired` →
  `conc_tracker.tracker_wired_single_thread_clean`. BUG-2 wired the tracker; the old name
  asserted the opposite. Body unchanged (it verifies no false-positive on clean single-thread
  create/destroy).

- **D3D12 honest guard.** `d3d12/queue.c` submit now loud-errors if `wait_count`/`signal_count`
  are non-zero (the lowering is Vulkan-only this milestone) rather than silently dropping them
  (MEL-ENGINE-VIII).

- **Tests.** Three new `conc_submit` tests: `signal_then_wait_binary`, `timeline_signal_and_wait`,
  `sync_destroy_after_use` (the H3 armed path). All exercise the new surface end-to-end on
  MoltenVK.

## Verification (integrated branch, Apple M3 Pro / MoltenVK 1.4.1 / Vulkan 1.2.334, validation on, NOFORK)

`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-* macos --gpu=vulkan`:
gpu-foundation 8/8 · gpu-vulkan 48/48 · gpu-stress 20/20 · gpu-concurrency **13/13** (10+3 new) ·
gpu-visual 11/11 · gpu-bench 12/12 = **112** macOS tests (was 109). Zero VUID, zero unexpected
validation. `hello-gpu` builds + packages under both `--gpu=vulkan` and `--gpu=metal`.

**Leak check.** gpu-vulkan reports `1 MB of GPU memory still allocated` on ~36 texture-creating
teardowns. Verified **pre-existing, not a regression**: stashed all round-5 changes, the clean
base (`f68233e`) shows the identical 36; restored, still 36. Root cause is the base codebase's
`device_destroy` not draining deferred texture/RT frees before tearing down the device — out of
scope here, flagged below.

## Kludges & debt (confessed — MEL-ENGINE-VIII)

- **§5.2 wired on Vulkan only.** D3D12's `queue_submit` does not honor wait/signal (loud-guarded,
  not lowered); it needs `ID3D12CommandQueue::Wait`/`Signal` against the timeline fence. Metal's
  is loud-stubbed (M4). M2's co-primary premise is not met for the submit-sync surface until D3D12
  lands its lowering — and that is itself blocked on win-pilot being reachable to verify.
- **D3D12 caps + guard edits are UNCOMPILED.** win-pilot offline all session; the
  `d3d12/caps.c` cohort writes, the `TiledResourcesTier` sparse derivation, and the `d3d12/queue.c`
  guard passed static review only. Syntactically conservative (bool/enum assignments, one
  `>=`-tier branch, one log line) but unverified — same standing debt as round 4.
- **L2 partial.** Swapchain substitution is now logged, but `swapchain_create` still returns a
  bare pointer with no §3.2 status, so the warning cannot ride the success-with-degradation
  channel the spec mandates. The reshape is deferred for blast radius.
- **L3 partial.** Latch now resets, but the warning is still a log line, not a device-level U2
  status/event. `pump->warned` remains a non-atomic advisory latch (benign race, pre-existing
  shape).
- **Wait dst-stage is `ALL_COMMANDS`.** Correct but conservative; no per-entry stage granularity
  yet (additive later).
- **Pre-existing device-teardown leak (not mine).** `device_destroy` leaves deferred
  texture/RT memory unfreed (~1 MB/device in gpu-vulkan). Present on the base; surfaced here only
  because I was hunting for regressions.
- **audit M4 (indirect handle family) deliberately NOT done — see below.**

## audit M4: scoped out, with rationale

The round-4 audit listed M4 (imports return the *direct* handle family; the `Mel_Gpu_*_Indirect`
peers don't exist) in the Medium cohort, and the selected lane named it. I excluded it on purpose:
`design/gpu-rhi.md` §3.1 *realization-status* explicitly marks the indirect-for-imports family —
`Mel_Gpu_Buffer_Indirect` / `_Texture_View_Indirect` / `_Accel_Struct_Indirect`, `*_bindless_slot`
on indirect handles, `*_make_indirect`, the per-type `*_indirect_destroy` entry points — as
**target-state, not yet in the public headers**, and the auditor itself scoped it as "record for
the type-system pass." Implementing it is a substantial type-system change the spec deliberately
phases; a partial version would trip the "no half-implementations" prohibition. Recommend a
dedicated indirect-handle-family pass rather than folding it into a cleanup sweep. (Rebutting the
premise that M4 was a cheap cohort fix — MEL-CODE / honesty.)

## Open items needing Gabbo

- Bring win-pilot up, then `git pull && C:\Users\Gabbo\dev.cmd nob test gpu-d3d12` — verifies both
  the round-4 D3D12 work (17+5) **and** this round's `d3d12/caps.c` + guard edits compile/pass.
- Decide priority of the D3D12 wait/signal lowering (closes the §5.2 co-primary gap) vs. the
  forward step (B1 Vulkan bindless set-split from `design/gpu-bindless-growable.md`).
- Sanction (or reject) the deferred L2 swapchain `{value,status}` reshape and the L3
  device-event warning channel as their own small tasks.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document the mandatory macOS GPU-suite invocation
  (`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-* macos --gpu=vulkan`)
  in `modules/gpu/readme.md` — it is load-bearing (MoltenVK cannot survive `fork()`) and still
  undocumented. (Recurring suggestion from round 4; left to the readme, never CLAUDE.md.)

## Suggestions

- Fix the pre-existing `device_destroy` deferred-free drain so texture/RT memory is reclaimed at
  teardown (gpu-vulkan would return to 0 MB). Small, contained, and removes a real latent leak.
- `d3d12/queue.c` carries its own `stackbuf[8]` MEL-CODE-002 fixed array (command lists) — fold
  into the same dynamic-array treatment when the D3D12 box is reachable.
- Next forward step once §5.2 closes on D3D12: B1 Vulkan bindless set-split, the no-prereq root of
  `design/gpu-bindless-growable.md`.
