# GPU RHI — concurrency/stress round 3 (tracker end-to-end, copy-under-lock hammer, perf cliff)

Round-3 redteam off `origin/main` (`b0ab013`, comment-free) after round-2 landed BUG-1 (copy-under-lock)
and BUG-2 (wired thread-safety tracker, report-not-abort). Host: Apple M3 Pro / MoltenVK 1.2.334 (8 hw
threads). All runs `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test <target> macos --gpu=vulkan`.
Edits confined to `modules/gpu/test/test_concurrency.c`, `modules/gpu/test/test_stress.c`, this writeup.

## Results

- **`gpu-concurrency` 10/10** (7 prior + 3 new), stable 8/8 repeat runs. 0 VUID, 0 validation error, 0 `leak:`;
  every device-creating probe `Destroyed VkPhysicalDevice … with 0 MB of GPU memory still allocated`.
- **`gpu-stress` 20/20** (16 prior + 4 new), stable 8/8 repeat runs. Same grep-clean.
- **Sibling suites unregressed:** `gpu-vulkan` 37/37, `gpu-foundation` 8/8.

## Task 1 — SerializedPerObject tracker, end-to-end (genuine RHI misuse, capture + survive, no false positives)

Round-2's `vk_tracker.cross_thread_misuse_reports_without_aborting` drove the **bare** tracker object
directly. Round-3 drives the misuse **through the public RHI call path** and **captures the diagnostic** with a
custom `Mel_Log_Sink` (`level_threshold = MEL_LOG_ERROR`, `str8_contains(msg, "thread-safety violation")`,
atomic counter; `mel_log_sink_flush_all()` before reading, because the log writer is a background thread).

- **`conc_tracker.rhi_same_buffer_write_reports_without_aborting` (positive, deterministic).** Two threads
  `mel_gpu_buffer_write` the **same** device-local buffer handle, barrier-gated per iteration, 24 iterations.
  `buffer_write` on a device-local buffer takes the slow `mel_gpu__staging_upload` path (a full per-call
  staging buffer + command pool + fence + `vkQueueSubmit` under `submit_lock` + `vkWaitForFences`), so the
  tracker's `enter`→`exit` bracket spans a wide window. The barrier release lands both threads inside the
  bracket every time. Observed: **exactly 24 of 24 `thread-safety violation (§3.7)` reports captured, every run
  (5/5)** — the cross-thread `SerializedPerObject` use on a resource mutate is caught deterministically, the
  `[ERROR]` line is real and observable, **the process survives** (no abort under NOFORK), the buffer stays
  alive, and the device tears down with 0 MB leaked. This is Vulkan-safe: each `buffer_write` uses its own
  staging objects and the submit is `submit_lock`-serialized, so there is no concurrent-raw-Vulkan-on-one-object
  hazard — only contractually-illegal concurrent writes to one resource, which is exactly the misuse class the
  tracker must flag.
- **`conc_tracker.rhi_distinct_buffer_write_no_false_positive` (negative).** T threads each create/write/write/
  destroy their **own** device-local buffers concurrently (distinct `track_key`s). Captured violations: **0**.
  Correct concurrent use produces no report.
- **`conc_tracker.rhi_destroy_then_recreate_slot_no_false_positive` (negative).** 256 create→destroy rounds
  reusing the same slot index single-threaded; the synthesized `track_key` mixes `(table_ptr, slot_index)`, so a
  reused slot yields the same key — this probe proves the immediate destroy retires the tracker entry so the
  next create/destroy on the reclaimed slot does **not** false-positive. Captured violations: **0**.

Why the command-list-recording misuse class is **not** shipped as a live RHI probe: a second thread entering
`command_list_begin` on the same `cmd` would have both threads call `vkResetCommandBuffer`/`vkBeginCommandBuffer`
on one `VkCommandBuffer` concurrently — genuine Vulkan UB that MoltenVK need not survive, risking a runner crash
unrelated to the tracker. The recording bracket's wiring is verified structurally (`record.c:33/47`) and the
buffer-mutate path proves the same `track_enter/exit` mechanism end-to-end; the bare-object recording-class
analogue is covered by `conc_tracker.distinct_objects_and_concurrent_class` and round-2's `vk_tracker` probe.

## Task 2 — hammer the copy-under-lock fix (BUG-1 truly closed; no new race in the ~43 converted sites)

- **`stress_alloc.mixed_type_churn_storm`.** Round-2's storm exercised only the **buffer** path end-to-end
  (confessed gap). This extends the same mapped-range overlap ledger + sentinel readback to a per-step
  create/destroy of a **buffer + texture + sampler** triple (three distinct `table_get_copy` site families),
  8 threads × 16000 steps × 64 slots, with a per-step `*_alive` liveness check on all three handles
  (`type_breaks` counter). Result: **0 overlaps, 0 sentinel corruptions, 0 liveness breaks** across 8/8 runs.
  A reopened interior-pointer escape on any of the converted texture/sampler sites would surface as a liveness
  break or (for the buffer) an overlap; none did.
- **`stress_command.submit_and_destroy_storm_interleaved`.** Round-2 noted `threaded_deferred_free_under_submit`
  does NOT exercise the BUG-1 window (the consumed buffer's record is read before the destroy storm relocates
  it). This pits half the threads doing create→submit→readback→deferred-destroy against the other half running a
  tight create-map-destroy churn storm (6000 rounds) on the **same device**, maximizing the create-vs-destroy
  relocation window while a submitter holds a record across `queue_submit`. Result: **0 overlaps, 0 corruptions**,
  8/8 runs. The deferred-free record read survives the concurrent destroy storm.
- **`stress_bindless.threaded_sampler_dedup_refcount`.** Targets the **one** in-place record mutation round-2
  kept as an under-lock RMW rather than a copy (`mel_gpu__sampler_refcount_add`, the U11 dedup claim count). The
  main thread holds an anchor claim on a dedup key; T threads each create+destroy 256 samplers with the **same**
  key (net-zero per worker, every create dedups to the anchor handle). A lost RMW update would spuriously drop
  the anchor's refcount and free it under the live main-thread claim. Result: every worker's create returns the
  anchor handle (`handle_breaks = 0`), the anchor is **alive throughout** (`dead_while_claimed = 0`), and only the
  main thread's final destroy frees it. The refcount RMW is correct under contention (it is serialized by
  `sampler_lock` outer + `obj_lock` inner, consistent lock order, no deadlock).

## Task 3 — perf-sanity the lock (no NEW cliff from copy-under-lock)

`stress_perf.copy_under_lock_no_new_cliff` runs identical create+destroy work 1-thread vs 8-thread (best-of-3
after a warmup, to damp first-touch noise) and asserts the 8-thread/1-thread speedup does not collapse below a
generous `0.35×` floor. Measured: **1-thread ≈ 49.9 ms, 8-thread ≈ 45.7 ms, speedup ≈ 1.04–1.09× across runs.**
This is *not worse* than — in fact slightly above — round-2's reported `0.81×` `obj_lock` baseline, so the
copy-under-lock `memcpy`-while-holding-`obj_lock` introduced **no pathological cliff**. (The absolute number is
device-local buffer create/destroy, heavier per-op than round-2's bench, which is why ~1× rather than sub-1×; the
test is a regression floor, not a strict cross-round comparison.) The underlying spec deviation is unchanged and
**not** in scope this round: the slotmap is still a single per-device mutex, not §3.7/U1's promised lock-free MPMC.

## NEW confirmed bugs

**None.** The three hardening storms (mixed-type, submit-vs-destroy, threaded dedup) and the perf floor are all
green and stable, which is the *expected* outcome if BUG-1/BUG-2 are genuinely closed and copy-under-lock added
no cliff. No expected-failure probe was shipped this round because no defect was reproduced (we do not fake a
red, and we do not fake a green). The tracker now reports correctly with zero false positives.

## Kludges (MEL-ENGINE-VIII — full confession)

- **Stale test name left in place.** `conc_tracker.device_accepts_flag_but_tracker_is_unwired` (round-2 vintage)
  asserts the tracker is *unwired*; round-2's BUG-2 fix wired it, so the **name and premise are now false**. The
  test still passes (it creates one buffer single-threaded → no cross-thread misuse → no report), so it is not a
  lie about behavior, but the name misleads. I did **not** rename or delete it: other agents/writeups reference
  it by name, and renaming risks churn outside the value this round adds. My new
  `rhi_same_buffer_write_reports_without_aborting` is the authoritative proof the tracker is wired. Recommend the
  builder/Gabbo rename it to `…tracker_is_wired_single_thread_clean` (or delete it) in a follow-up.
- **The deterministic positive leans on an implementation detail** (`buffer_write` on a device-local buffer takes
  the slow staging-upload path, widening the tracker bracket). If a future optimization makes device-local
  `buffer_write` fast/async, the 24/24 determinism could degrade to a probabilistic catch. The barrier-per-
  iteration + 24 iterations keeps it overwhelmingly likely even then, but the *exactly-24* guarantee is
  staging-path-dependent. Flagged, not load-bearing for the assertion (`viol > 0`, not `viol == 24`).
- **`track_key` collision risk inherited, not re-tested.** Round-2's `mel_gpu__track_key` synthesizes the object
  token by mixing `(table_ptr, slot_index)` (non-injective by construction). My false-positive probes exercise
  the same-slot-reuse case (no collision observed), but I did not construct an adversarial `(table,index)`
  collision — that would require two distinct tables hashing to one key, which the public API cannot force from a
  test. Inherited debt; out of test scope.
- **Worktree cold-build cost.** This agent's edits are isolated in the worktree (`.claude/worktrees/…`), which
  shares no build cache with the main checkout, so the first `nob test` cold-rebuilt the entire third-party tree
  (gmp/mpfr/…). Operational only — no source/build.c shortcut; subsequent runs are warm and fast.

## Coverage still missing

- **Command-list-recording cross-thread misuse is not driven through the RHI** (Vulkan-UB hazard on a shared
  `VkCommandBuffer`, see Task 1). Covered structurally + via the bare-object analogue only.
- **`internally_synchronized` queue `Concurrent` submit** (§5.2) remains untested — the headless device lowers
  all queue roles to one graphics queue, so `queue_submit` is `SerializedPerObject` in practice here. (Round-2
  gap, unchanged.)
- **Pipeline/shader/bind-group/sync `table_get_copy` sites** are exercised by the slot-collision and churn
  suites but not by a per-type overlap-style sentinel (those records carry no host-mapped range to ledger). The
  mixed-type storm + the shared root cause (one `mel_slotmap_get` primitive behind all `table_get_copy` callers)
  make the scope clear; a per-type teardown-order fuzz would add marginal assurance.
- **The slotmap-MPMC spec deviation** (§3.7/U1 "lock-free MPMC" vs one device mutex, measured ~0.73–0.81×
  8-thread) is re-confirmed by both `conc_slotmap.serialization_is_measured` and the new perf probe; still a
  spec-vs-reality reconciliation for the builder/Gabbo, not a correctness bug.

## CLAUDE.md suggestions (recommendations only — not edited)

- A sanctioned `--sanitize=thread` build flag (raised rounds 1–2) remains the single highest-leverage toolchain
  add for this module; round-3's hardening storms are probabilistic guards that TSan would make deterministic.
- The `modules/gpu` comments ruling (global "Never write comments" vs the module's former house style) is now
  *settled in practice* — `origin/main` is comment-free and this round honored the HARD RULE (zero comments
  added). Worth recording the ruling explicitly so it stops resurfacing.

## For the builder (Vulkan) / Gabbo

- **Builder:** nothing to fix — BUG-1 and BUG-2 are confirmed closed under heavier round-3 stress (mixed-type
  storm, submit-vs-destroy storm, threaded dedup refcount), the tracker reports genuine cross-thread
  `SerializedPerObject` misuse end-to-end with zero false positives, and copy-under-lock added no perf cliff
  (~1.04–1.09× at 8 threads, ≥ round-2's 0.81×). Two **non-bug** follow-ups: (1) rename/retire the stale
  `device_accepts_flag_but_tracker_is_unwired` test; (2) reconcile the §3.7 "lock-free MPMC" promise with the
  measured single-mutex slotmap.
- **Gabbo:** the `modules/gpu` comments ruling is effectively resolved (comment-free); recording it explicitly
  would close the recurring tension.
