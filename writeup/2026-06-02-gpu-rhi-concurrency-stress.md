# GPU RHI — multi-threaded concurrency audit & stress extension (M2, round 2)

Round-2 redteam closing the round-1 audit's single biggest gap: no multi-threaded U36 probe, and an
unverified claim that the slotmap is per-device-mutex-serialized rather than the lock-free MPMC §3.7/U1
assume. Host: Apple M3 Pro / MoltenVK 1.2.334 (8 hardware threads). Both deliverables run under
`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test <target> macos --gpu=vulkan`.

## Results

- **`gpu-concurrency` (new): 7 passed, 0 failed, 0 skipped.** Grep-clean: 0 VUID, 0 leak, 0 validation error;
  every device-creating probe `Destroyed VkPhysicalDevice … with 0 MB still allocated`. Stable 7/7 across 6
  repeat runs. Probes: `conc_create.{distinct_handles_no_slot_collision, bindless_slot_equals_index_under_contention}`,
  `conc_record.per_thread_cl_record_and_submit`, `conc_write.distinct_resource_sentinels`,
  `conc_slotmap.serialization_is_measured`, `conc_tracker.{distinct_objects_and_concurrent_class,
  device_accepts_flag_but_tracker_is_unwired}`.
- **`gpu-stress` (extended): 15 passed, 1 failed (expected-failure), 0 skipped, of 16.** The single failure is
  the new `stress_alloc.threaded_overlap_storm`, the documented regression guard for **BUG-1** below (it fires
  8/8 runs at STEPS=8000). The other new probe, `stress_command.threaded_deferred_free_under_submit`, passes.
  Grep-clean apart from BUG-1's own intentional `[ERROR]` diagnostic and the pre-existing intentional
  `MissingBindlessSlot` line: 0 VUID, 0 leak, 0 validation error.
- **Baselines unregressed:** `gpu-vulkan` 32/32, `gpu-foundation` 8/8.

## Slotmap serialization — measured, claim confirmed

§3.7 / U1 specify the slotmap-per-type as a "lock-free MPMC allocator", so `Concurrent` creation should scale
with threads. It does not. `conc_slotmap.serialization_is_measured` runs the same total create+destroy work on
1 thread vs 8 and reports:

    1-thread 14.7 ms, 8-thread 18.2 ms, speedup 0.81x

Eight threads are **slower** than one — the signature of a single global lock plus contention/cache-line
bouncing, the polar opposite of an MPMC allocator approaching 8×. Source-confirmed: every slotmap op of every
resource type funnels through ONE per-device mutex `obj_lock` — `mel_gpu__table_insert/get/remove/
remove_deferred/reclaim` each lock `dev->obj_lock` around the bare `mel_slotmap_*` call
(`src/vulkan/device.c:377-414`). Not even per-type; one coarse device-wide lock. The round-1 `gpu-rhi-m1`
confession is verified, now with a number. Not a correctness bug (the lock makes create correct), but a direct
spec deviation (§3.7 promises lock-free MPMC) and a scalability ceiling for the resource-creation hot path
(MEL-ENGINE-III). The fixer should reconcile the spec with reality: either ship a real lock-free/sharded
slotmap, or amend §3.7 to state the create path serializes on a device lock.

## NEW confirmed bug

### BUG-1 (HIGH — host-memory corruption) — post-unlock interior-pointer race on the packed slotmap; concurrent `buffer_create`/`buffer_destroy` alias live device memory

`modules/collection/src/slotmap.c:142` (the interior pointer) consumed by **43 call sites** across
`src/vulkan/{buffer,texture,sampler,pipeline,shader,sync,bind_group,interop,device}.c`; representative:
`src/vulkan/buffer.c:188-206` (`buffer_destroy`), `:245-261` (`buffer_write`/`buffer_mapped`).

`§3.7` declares `buffer_create` / `buffer_destroy` **`Concurrent`** across distinct handles. The backend tries
to honor that by serializing each slotmap op behind `obj_lock` and each buddy op behind the allocator lock —
both correct **in isolation**. The defect is the pointer `mel_gpu__table_get` hands back: it points **into the
slotmap's packed dense array** (`slotmap.c:142`, `data + packed_idx*item_size`). Callers dereference it
**after `obj_lock` is released** (each `table_*` helper locks/unlocks internally; the caller holds nothing).
While a reader holds that pointer:

- a concurrent `table_insert` (another thread's `*_create`) can **realloc-grow the packed array**
  (`slotmap.c:104-106,118`), and
- a concurrent `table_remove` (another thread's `*_destroy`) **swap-remove-`memcpy`s the last element over the
  freed packed slot and rewrites `packed_idx`** (`slotmap.c:165-172`),

relocating the element under the live reader. The reader then sees a **torn `Mel_Gpu_Buffer_Obj`** — a stale
`alloc.offset` / `alloc.mapped` / `buf`. `buffer_destroy` then `mel_buddy_free`s the **wrong** range, and a
later `buffer_create` binds memory **over a still-live allocation**. Result: two simultaneously-live
host-visible buffers share device memory → silent host-memory corruption (MEL-ENGINE-VIII forbids exactly
this). The Vulkan validation layer cannot see it (no API misuse; pure host-side aliasing), which is why
round-1 (single-threaded) missed it entirely.

**Isolation work proving the root cause is the gpu glue, not the buddy:**

- The buddy allocator is correct single-threaded: a standalone fuzz (64 MiB block, 256 B min, odd 17..8208 B
  sizes, 2 000 000 create/free ops × 8 seeds) found **zero** overlaps.
- A standalone TSan harness mirroring the gpu locking exactly (one mutex spanning each `mel_buddy_alloc` /
  `mel_buddy_free`, the live-range ledger guarded separately) found **zero** overlaps and **zero** races — a
  span-locked buddy never aliases. So the fault is exclusively the **post-unlock interior-pointer read** in the
  gpu layer, not the buddy and not the mutex (which is a genuine `pthread_mutex_t`).
- The in-suite probe `stress_alloc.threaded_overlap_storm` registers every live buffer's actual mapped
  `[ptr, ptr+size)` in a shared mutex-guarded ledger and flags overlap at insert time — a deterministic catch.
  It reports e.g. "11 overlapping live mapped-ranges, 9 sentinel corruptions" and fires 8/8 runs at STEPS=8000.

**Fix direction (for the fixer):** the obj_lock must guard the *use* of the record, not just the slotmap
bookkeeping. Either (a) have the table accessor copy the resource record **out by value while holding
obj_lock** and return the value (no interior pointer escapes the lock), or (b) hold `obj_lock` across the whole
create/destroy/write critical region that touches `o`. (a) is the minimal, idiomatic fix and composes with the
existing helpers. Note this is *distinct* from the §3.7 "lock-free MPMC" deviation above: even the current
single-mutex design is incorrect because the lock does not extend to the dereference.

Why `gpu-concurrency`'s create/write probes stay green while the storm goes red: those probes have each worker
operate on its **own** private resource array and destroy serially on the main thread afterward, so there is no
tight create-vs-destroy interleaving relocating a slot under a live reader. The storm interleaves
create+destroy per-thread on one shared device, maximizing the relocation window. The separation is deliberate:
the concurrency suite validates the positive §3.7 contract; the stress suite carries the bug guard.

## NEW confirmed bug

### BUG-2 (MEDIUM — dead debug aid) — the U21/§3.7 thread-safety tracker is allocated but never invoked on any public call

`src/vulkan/device.c:280-281` (and `src/d3d12/device.c:121-122`) create `dev->tracker` when
`device.debug.thread_safety_tracker = true`, and tear it down at device-destroy — but **no public call path
anywhere invokes `mel_gpu_thread_tracker_enter` / `mel_gpu_thread_tracker_exit`** (source-confirmed: the only
callers of those entry points in the whole tree are these tests and `gpu-foundation`). §3.7 promises "every
public call records the calling thread and the object class; double-entry on a `SerializedPerObject` object
from a different thread without an intervening retirement asserts loudly … the single most useful debug aid for
porting from a single-threaded prototype to a multi-threaded renderer." That promise is **unrealized**: the
tracker is dead infrastructure that can never fire on real RHI misuse. `conc_tracker.device_accepts_flag_but_
tracker_is_unwired` documents the gap honestly — it requests the flag (the device must still create), runs two
threads through `buffer_create`/`destroy` that *would* be the kind of `SerializedPerObject` misuse the tracker
must catch, and they run clean **precisely because the tracker is unwired**. We do not fake a catch
(MEL-ENGINE-VIII).

The tracker *object* itself is correct and is exercised under real threads by
`conc_tracker.distinct_objects_and_concurrent_class`: `SerializedPerObject` on distinct per-thread objects does
not assert, the `Concurrent` class is a true no-op (never registers an owner), and a fresh `SerializedPerObject`
enter after all the no-op traffic still succeeds (no residue). The illegal cross-thread case — a
`SerializedPerObject` object entered by thread A then by thread B without an intervening exit — would fire the
tracker's live `mel_assert(owner == self)` and abort the process; under `MEL_TEST_NOFORK=1` that takes the whole
runner down, so we do NOT ship it as a live probe (its reality is guaranteed by construction at
`src/threading.c:67`). **Fix direction:** wire `enter`/`exit` into every public entry point per its documented
class (the §3.7 default table), guarded by `dev->tracker != NULL`, so the assert can actually fire — and so it
would catch BUG-1's misuse class for free.

## What the new probes cover (and how they obey the harness)

The test runner arms a single `setjmp` per test on the **main** thread; `MEL_REQUIRE`/`MEL_FAIL`/`MEL_SKIP`
`longjmp` there. A `longjmp` across a thread boundary is undefined and would crash the runner. So every worker
thread records outcomes into `_Atomic` fields and the **main thread evaluates all assertions after `join`** —
the only safe shape under `NOFORK`. All waits are bounded (barrier-synchronized start, fixed iteration counts)
so a worker cannot deadlock the runner. Thread count is `clamp(hardware_concurrency, 2, 8)`.

- `conc_create.distinct_handles_no_slot_collision` — M threads each `buffer/texture/sampler_create` in a tight
  loop; main thread asserts zero create/alive failures, exact total, and **no slot-index collision** across all
  simultaneously-live handles per type (a torn/double-allocated slot would collide).
- `conc_create.bindless_slot_equals_index_under_contention` — same under a bindless device; every view/sampler
  satisfies §3.1 `bindless_slot == handle.index` under contention, indices collision-free.
- `conc_record.per_thread_cl_record_and_submit` — one CL per recording thread from the per-thread per-family
  TLS pool (U15, `mel_gpu__thread_pool` keyed on thread id), record + submit on a shared queue; every
  per-thread record+submit resolves OK (`SerializedPerObject` on the CL, queue serialized by `submit_lock`).
- `conc_write.distinct_resource_sentinels` — `Concurrent` `buffer_write` across **distinct** host-visible
  resources from many threads, sentinel readback; `SerializedPerObject` on the same resource is UB by contract
  and is left untriggered.
- `conc_slotmap.serialization_is_measured` — the 1-vs-8-thread timing above; logs the measurement, asserts only
  correctness.
- `stress_alloc.threaded_overlap_storm` — the BUG-1 expected-failure guard (mapped-range overlap ledger +
  sentinel readback under a concurrent create/destroy storm).
- `stress_command.threaded_deferred_free_under_submit` — N threads each create→submit→destroy-immediately a
  device-local buffer (deferred-free gated on the submit serial, §3.3), with a READBACK copy proving the GPU
  saw valid contents at submit time; passes (the deferred path's `submit_lock` serialization keeps those reads
  consistent — it does NOT exercise the BUG-1 window because the consumed buffer's record is read before the
  concurrent destroy storm relocates it).

## Kludges I introduced (MEL-ENGINE-VIII — full confession)

- **`threaded_overlap_storm` is a deliberately-RED expected-failure probe.** A data-race guard is intrinsically
  probabilistic; I sized STEPS to 8000 / 96 slots so the deterministic overlap ledger fires with overwhelming
  probability (8/8 observed), but I cannot make it *certain* without bounding the run unreasonably. It stays red
  until the fixer closes BUG-1; it must NOT be quarantined or inverted to green. This is the honest call (do not
  fake a pass, do not crash the runner) and it does keep `gpu-stress` non-green until the fix lands — that is
  the intended signal, not a defect of the probe.
- **The BUG-1 illegal cross-thread tracker case and the BUG-1 over-cap registration cliff are NOT shipped as
  live probes** — each fires a live `mel_assert` that aborts the whole `NOFORK` runner. Documented here; the
  tracker case becomes shippable once BUG-2 wires the tracker to return/report instead of asserting-as-control,
  per §3.7's "reported through `mel_gpu_provider_call_class`" shape.
- **Root-cause isolation used throwaway harnesses** (a single-threaded buddy fuzz and a TSan harness compiled
  against `buddy.c`) outside the build system; both were deleted after use. No `src/**` or non-deliverable
  build.c edit survives. A momentary TSan cflag injected into the shared `gpu` library to localize the race was
  reverted immediately — the harness boundary (build.c = my target only) is correct and I honored it.
- **No probe touches over-cap bindless registration** — the fixer is changing that semantic this round
  (fixed-cap+status → grow-on-demand); I stayed off it per the boundary.

## Coverage still missing

- **`SerializedPerObject`-on-same-object violation paths are unverified end-to-end** because the tracker is
  unwired (BUG-2) and a real misuse would `mel_assert`-abort the `NOFORK` runner. Once BUG-2 is fixed and the
  tracker reports rather than only asserts, a real negative test (`buffer_write` on one resource from two
  threads, expect the tracker to flag) becomes shippable.
- **`internally_synchronized` queue `Concurrent` submit** (§5.2) is untested — the headless device lowers all
  queue roles to one graphics queue, so there is no genuinely-concurrent queue to submit to; `queue_submit`
  remains `SerializedPerObject` here in practice.
- **Cross-thread future resume** (register on thread A, resume on thread B via `mel_reactor_post`, §3.3) is
  partially covered by `gpu-foundation.future.cross_thread_wait` but not under a real device pump (the headless
  device has no reactor; submit resolves synchronously on the fence).
- **The 43 BUG-1 call sites are confirmed by pattern, but only the buffer path is exercised end-to-end.** The
  texture/sampler/pipeline records have the same interior-pointer escape; a per-type overlap storm would prove
  each, but the buffer storm + the shared root cause (one `mel_slotmap_get`) make the bug's scope clear.

## CLAUDE.md suggestions (recommendations only — not edited)

- The repo would benefit from a sanctioned, documented way to run a target under ThreadSanitizer (a build flag
  or env hook). Root-causing BUG-1 required a hand-rolled out-of-tree TSan harness; a first-class
  `--sanitize=thread` would have localized the interior-pointer race in minutes and belongs in the toolchain
  for any module with internal concurrency.
- The "comments Rule-#1 tension" the round-1 writeup raised still stands: global `~/CLAUDE.md` says "Never write
  comments"; `modules/gpu` is densely commented in the house style the project `CLAUDE.md` encourages, and my
  new test files match the surrounding commented style. Needs a ruling for this module.

## For the fixer / Gabbo

- **Fixer:** BUG-1 (interior-pointer race) is the headline — it is silent host-memory corruption on the
  `Concurrent` create/destroy path §3.7 promises, caught deterministically by `threaded_overlap_storm`. Minimal
  fix: copy the resource record out by value under `obj_lock`. BUG-2 (wire the thread-safety tracker into the
  public call path) is the second — it would have caught BUG-1's misuse class for free, and it makes §3.7's
  marquee debug aid real. The slotmap-serialization deviation (§3.7 "lock-free MPMC" vs one device mutex) is a
  spec-vs-reality reconciliation, measured at 0.81× 8-thread speedup.
- **Gabbo:** the comments Rule-#1 ruling for `modules/gpu` (above) is still open from round 1.
