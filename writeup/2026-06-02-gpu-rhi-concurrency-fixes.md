# GPU RHI — concurrency fixes (BUG-1 host-memory corruption, BUG-2 unwired tracker)

Patcher round closing the two defects the round-2 redteam confirmed in
`2026-06-02-gpu-rhi-concurrency-stress.md`. Host: Apple M3 Pro / MoltenVK 1.2.334.
Build/test: `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test <target> macos --gpu=vulkan`.

## Results

- **`gpu-stress` 16/16.** `stress_alloc.threaded_overlap_storm` (the BUG-1 regression guard, previously RED 8/8)
  now PASSES — verified across **19 consecutive runs** (9 + 10 batches), deterministic, not lucky. The guard was
  not weakened (`test_stress.c` untouched).
- **`gpu-vulkan` 37/37** — 36 prior + the new BUG-2 negative probe `vk_tracker.cross_thread_misuse_reports_without_aborting`.
- **`gpu-concurrency` 7/7**, **`gpu-foundation` 8/8** — unregressed; the tracker wiring produces no false positives.
- **Validation / leak clean** on every suite: 0 VUID, 0 validation error, 0 `leak:`; every device-creating probe
  reports `Destroyed VkPhysicalDevice … with 0 MB of GPU memory still allocated`.

## BUG-1 — post-unlock interior-pointer race (HIGH, host-memory corruption)

**Root cause (confirmed).** `mel_slotmap_get` returns an interior pointer into the packed dense array
(`slotmap.c:142`, `data + packed_idx*item_size`). Every `mel_gpu__table_get` call site dereferenced that pointer
*after* `obj_lock` released; a concurrent `table_insert` (realloc-grow) or `table_remove` (swap-remove memcpy)
relocated the element under the live reader, tearing `alloc.offset`/`alloc.mapped`/`buf`. `buffer_destroy` then
freed the wrong buddy range → two live host-visible buffers aliasing device memory. §3.7 declares create/destroy
`Concurrent` across distinct handles, so the read of the record must be lock-guarded, not only the slotmap
bookkeeping. (The slotmap and the span-locked buddy are each correct in isolation; the fault was exclusively the
gpu-layer interior-pointer escape.)

**Fix — copy-under-lock, idiomatic, composes with the existing table wrappers.** Added to `device.c` /
`vk_backend.h`:

- `mel_gpu__table_get_copy(dev, t, h, out)` — `memcpy`s the whole record into caller storage *while holding*
  `obj_lock`; no interior pointer escapes. Returns false (out untouched) on a stale handle, so use-after-free
  stays loud (MEL-ENGINE-VIII).
- `mel_gpu__table_alive(dev, t, h)` — returns only the liveness boolean for the `*_alive` entry points (liveness
  is read from the sparse slot array, never the packed payload, so it cannot be torn).
- `mel_gpu__sampler_refcount_add(dev, h, delta, out_after)` — the **one** in-place record mutation (the U11 dedup
  claim count) is a read-modify-write *under* `obj_lock` against the live slot, never on a copy (a copy-back would
  drop a concurrent claim and reintroduce the very torn-record class).

Every former `mel_gpu__table_get` deref site was converted to one of the three. The internal helpers
`mel_gpu__texture_get` / `mel_gpu__texture_view_get` / `mel_gpu__pipeline_obj` changed signature from returning an
interior `**` to filling a caller-owned `*` by value; their callers (record.c, command.c, bind_group.c, binding.c,
interop.c) snapshot to a stack value. `mel_gpu__bg_kind` now snapshots the bind-group layout. Sites touched:
`buffer.c, texture.c, sampler.c, shader.c, pipeline.c, sync.c, bind_group.c, binding.c, interop.c, command.c,
record.c`. Value-copy is correct for these records because they are immutable after insert (shader/pipeline/
bind-group-layout carry heap pointers, but the destroy that owns them is `SerializedPerObject`, so the snapshot's
pointer fields are valid for the call's duration). `modules/collection` was NOT touched (the slotmap is correct).

The legacy `mel_gpu__table_get` definition remains (it is the d3d12 backend's primitive too, a separate TU not
linked on macos/vulkan); on the vulkan side it is now unused.

## BUG-2 — wire the U21/§3.7 thread-safety tracker (MEDIUM, dead debug aid)

**Two parts.**

1. **Report, don't abort (`threading.c`).** A cross-thread `SerializedPerObject` entry previously fired
   `mel_assert(owner==self)`, which aborts the process — fatal under `MEL_TEST_NOFORK`, which is why the audit
   couldn't ship a live negative probe. Changed to a loud `mel_log_error` naming the object; the violating
   thread's call returns without corrupting the owner's ledger. Same-thread recursive re-entry (depth) is
   unchanged.

2. **Wire enter/exit into the public call path (`device.c` hooks + per-site calls).** Added
   `mel_gpu__track_enter/exit` (no-op unless `dev->tracker != NULL`, free in release) and `mel_gpu__track_key`
   (synthesizes a stable per-resource object token from the `(table, slot index)` pair, since handles aren't
   pointers). Wired per the §3.7 class table:
   - **Resource destroy** — `buffer/texture/texture_view/sampler/shader/pipeline/sync/bind_group/bind_group_layout_destroy`
     → `SerializedPerObject` on the destroyed handle.
   - **Resource mutate** — `buffer_write`, `texture_write` → `SerializedPerObject` on the resource.
   - **Command-list recording** — `command_list_begin`/`end` bracket the recording window with the owning thread on
     the `cmd` pointer (`SerializedPerObject`); the canonical one-CL-per-thread pattern (U15) keeps this clean.
   - **Queue submit** — `queue_submit` → `SerializedPerObject` on the queue, or `Concurrent` when the queue is
     `internally_synchronized` (§5.2); the bracket spans only the producer-side window, released before the
     GPU-completion wait.
   - **Resource create** is `Concurrent` (a tracker no-op that registers no owner) — deliberately NOT wired, since
     an explicit Concurrent enter/exit is dead code (the tracker early-returns) and adds noise with zero detection.

**New probe.** `vk_tracker.cross_thread_misuse_reports_without_aborting` (appended to `test_vulkan.c`, the only
test file in scope) ships the previously-unshippable negative case: two barrier-synchronized threads hold the same
object as `SerializedPerObject` simultaneously; the test asserts the **process survives** (the `[ERROR]` report
fires, no abort) and the tracker ledger is not left corrupt. The expected `thread-safety violation (§3.7)` log line
appears and is correct (fail-loudly without crashing the runner).

**Coverage.** Full load-bearing §3.7 family table is wired: destroy, mutate, CL-recording, submit, create (the
last as the explicit Concurrent no-op). **Deferred:** per-individual-`cmd_*` foreign-thread detection — recording is
bracketed at `command_list_begin`/`end` (catches a second thread that begins/records the same CL), not at each
`cmd_draw`/`cmd_bind`/etc. The begin/end bracket plus the canonical one-CL-per-thread pattern covers the realistic
misuse; finer per-call granularity across ~30 `cmd_*` functions was left for a follow-up to avoid over-broad churn.

## Kludges (MEL-ENGINE-VIII — full confession)

- **`mel_gpu__track_key` synthesizes the object token by mixing `(table_ptr, index)`** (FNV-style multiply-xor)
  rather than using a real per-object pointer, because RHI resources are handles, not pointers. Collisions are
  theoretically possible (two distinct `(table,index)` pairs colliding to one key); for a debug-only aid over the
  small live set this is acceptable, but it is not injective by construction. A real fix would key on a stable
  per-resource address (e.g. the slot's packed-to-slot identity), which the current slotmap doesn't expose
  cheaply. Flagged, not load-bearing for correctness.
- **The worktree was branched from `origin/main`, which lacked the round-2 guard + concurrency suite;** I merged
  local `main` (commit `9cc2eef`) into the worktree branch to obtain them. No source conflict. (Operational note,
  not a code shortcut.)

## For Gabbo

- **Rule-#1 tension — tracker assert→report.** §3.7 says misuse "asserts loudly"; this task's brief explicitly
  required *report, not crash* (so the wired paths don't abort the `NOFORK` runner). I honored the task brief and
  changed the cross-thread case from `mel_assert` to `mel_log_error`. This is a deliberate deviation from §3.7's
  literal wording, sanctioned by the task. If you want the spec's "assert" semantics back for a non-test build,
  the cleanest shape is a build/runtime flag selecting assert-vs-report; say the word.
- **Spec deviation noted, NOT fixed (per scope): the slotmap is a per-device mutex, not the lock-free MPMC §3.7/U1
  promise.** The round-2 audit measured 0.81× 8-thread speedup. Reconciliation (ship a sharded/lock-free slotmap,
  or amend §3.7 to state the create path serializes on `obj_lock`) is a separate decision — left untouched.
- **Comments Rule-#1 tension (still open from round 1):** global `~/CLAUDE.md` says "Never write comments"; this
  module is densely commented in the house style the project `CLAUDE.md` encourages, and my edits/new test match
  the surrounding commented style. Needs a ruling for `modules/gpu`.
- **Suggestion (toolchain):** a first-class `--sanitize=thread` (raised in the round-2 writeup) would have
  localized BUG-1's interior-pointer escape immediately; still worth adding for any module with internal
  concurrency.
