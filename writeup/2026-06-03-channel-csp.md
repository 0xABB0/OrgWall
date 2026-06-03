# Channel — CSP M→N substrate

## Work done

New top-level module `modules/channel/` (namespace `<channel/...>`, prefix `mel_channel_`):
CSP point-to-point, M producers → N consumers, typed by a fixed `item_size`, capacity `0`
(unbuffered rendezvous, direct handoff, no buffer copy) or `N>0` (allocator-fed bounded ring).

- `channel.h` — public API: `try_send`/`try_recv`, `send_future`/`recv_future`, `send`/`recv`
  (fiber-blocking), `close`, `sel_init`/`sel_try`/`sel_wait`, `op_send`/`op_recv`. Status is
  severity+bitset (`MEL_CHANNEL_CLOSED`/`WOULD_BLOCK` are bits, not an enum).
- `channel.c` — per-channel `Mel_Mutex`; ring + intrusive sender/receiver waiter queues
  (`Mel_ListNode` in `Mel_Channel_Op`); the single-CAS commit (`group_state`: PENDING→COMMITTED
  XOR PENDING→CLOSED); `close` (set flag, splice queues, fire each survivor); `try_*`; `sel_try`;
  the future flavor (one heap record per parked op, resolved on a target executor).
- `channel_fiber.c` — the fiber-blocking flavor (`send`/`recv`/`sel_wait`) parking on a
  `Mel_Counter`. Split out so the callback-flavor consumer never links the job runtime
  (`mel_signal_wait`/`mel_counter_wait` are job symbols).
- `channel_internal.h` — shared struct + locked helpers across the two TUs.
- Tests: `test_channel.c` (16, no job), `test_channel_race.c` (2 pthread concurrency),
  `test_channel_fiber.c` (9, job-backed). 27 total, all green.

### Protocols / memory orders

- Commit/close: `compare_exchange_strong(group_state, …, acq_rel, acquire)`; exactly one of
  complete/close wins a waiter. Slot `memcpy` precedes the CAS under the channel lock; the woken
  party's post-wait acquire load of `group_state` publishes it.
- Channel mutex release/acquire orders ring + queue mutations. No seq_cst anywhere.
- `select` acquires all candidate channels' locks in ascending address order (deadlock-free, no
  alloc — min-unlocked-by-scan), decides under all-locks-held (removes the orphaned-counterpart
  race), parks-or-completes, drops locks, waits, retracts losers.

### sizeof (arm64)

`Mel_Channel` 160 B (mutex storage dominant), `Mel_Channel_Op` 80 B, `Mel_Channel_Sel` 16 B.

### Test results

- `channel-core` 16/16, `channel-race` 2/2, `channel-fiber` 9/9.
- ThreadSanitizer (standalone `clang -fsanitize=thread`, build system untouched): core 16/16 and
  race 2/2 **clean** (MPMC 4×4×5000 and 3×3×3000; close-vs-parked-future single-outcome 2000
  rounds). The select/close-race + MPMC are covered TSan-clean via the pthread + future-parking
  paths.
- The job-backed fiber tests under TSan report a SEGV in `mel__thread_trampoline` and spurious
  "data race" at fiber-resume edges — the job runtime's hand-written assembly fiber switches carry
  no `__tsan_switch_to_fiber` annotations, so TSan cannot track the happens-before across a context
  switch. This is a property of `modules/fiber`/`modules/job` (out of scope: "touch only
  modules/channel"), not a channel defect; the identical synchronization is exercised TSan-clean by
  the pthread race tests.

## Kludges (MEL-ENGINE-VIII — confess all)

- **Fixed-size arrays in tests.** `Mel_Channel_Op ops[2]` (select sets are caller-fixed by nature)
  and thread/fixture arrays sized by a compile constant (`threads[N]`), mirroring the canonical
  `test_job.c`. The library proper has zero fixed arrays (ring + records are allocator-fed). Test
  scaffolding only; not in shipped code.
- **`select` is O(n²) in candidate count** (lock-all by repeated min-scan, dup-skip by inner loop).
  Deliberate: zero allocation, and select sets are tiny (libdill/Go-scale). If a consumer ever wants
  large select sets, thread an allocator and sort once.
- **Fiber flavor needs the job runtime at link time.** `mel_signal_wait`/`mel_counter_wait` live in
  `modules/job`, asserted on a worker fiber. Isolated to `channel_fiber.c`; documented in the
  readme. Not a leak into the callback flavor.
- **TSan coverage of the fiber flavor is indirect** (see above) — the genuine concurrency is proven
  by the pthread future-parking tests, not the fiber tests, under TSan.

## CLAUDE.md suggestions (recommendation only)

- Consider documenting that `Mel_Signal` is "green at counter 0" (wait returns immediately) while
  `Mel_Counter` is the park-until-zero primitive — the inversion is easy to get wrong when building
  a parker. Cost me one debug cycle.
- A nob `--sanitize=thread|address` mode would make concurrency modules' TSan runs first-class
  rather than hand-assembled. If added, annotating `modules/fiber`/`modules/job` with
  `__tsan_create_fiber`/`__tsan_switch_to_fiber` would let TSan cover the fiber flavors too.

## Suggestions

- The future-flavor allocates one record per parked op (the only alloc on the future path). A
  caller-owned `Mel_Channel_Future_Op` (embed the record, as `select`/blocking already do) would
  make even the future path zero-alloc. Left out to keep the future API a plain
  `(future, exec, alloc)` call.
- `mel_channel_destroy` asserts both queues empty; pairs well with a `scope`-driven teardown once
  `scope` lands.
