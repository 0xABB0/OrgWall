# vat

One OS thread as the unit of control and affinity: a vat hosts sources, a turn queue, and the
driver/waiter pair that decides how the thread relinquishes the CPU. The architecture is
`probe/main_design/README.md`; the integration plan and benchmark gates are
`design/vat-integration.md`. This module is a candidate replacement for `reactor` (and the loop
half of `app`); nothing depends on it yet — replacement is gated on friction-free apps and
competitive benchmarks.

## Why it exists

The incumbent `reactor` is a GLib-shaped prepare/check/dispatch loop with a fixed poll-set
capacity, heap-bound source allocation, and platform backends welded to its iteration. `vat`
separates the three policies the loop conflates — *what to wait on* (the Waiter, a vtbl over
kqueue/epoll/IOCP/bridge), *how to spend a turn* (the Driver, fair/unfair as objects, never
flags), and *what a concern contributes* (the Source: wakeables + deadline + budgeted drain) —
so every axis is replaceable without touching the others (MEL-ENGINE-IX, MEL-CODE-001
structurally: no mode enums anywhere).

Work is the executor waist's intrusive `Mel_Task`: posting is zero-alloc, never-fail,
armed-coalesced; the cross-thread path is `Mel_Mpsc` plus the Waiter's doorbell.
`mel_vat_executor()` realizes the loop-bound executor the executor module names as owed.

## Surface

- `vat/vat.h` — `mel_vat_open/close/run/step/quit/post/executor/is_owner`, the source contract
  (`wakeables`, `deadline`, `drain(budget)`, `cancel`), the Waiter and Driver vtbls,
  `mel_vat_waiter_kqueue` (darwin), `mel_vat_waiter_cocoa` (macos), `mel_vat_waiter_guest`
  (subordinate: never blocks, arms a `Mel_Vat_Embedder` — `schedule_work` for timeout `0`,
  `schedule_delayed_work` otherwise, negative delay meaning wake-only-on-ring; its `arm`
  refuses wakeables, loudly, until the bridge wave), `mel_vat_driver_fair(alloc, budget)`.
- `vat/timer.h` — a shipped source multiplexing N deadlines behind one min-heap; fires by
  posting the registered `Mel_Task`.
- `vat/vsync.h` — a display-paced source (macos: CVDisplayLink); the CV thread sets an atomic
  flag and rings the doorbell, the source reports deadline `0` while signaled and drains one
  frame callback per vblank.

The min-deadline reduction is the relinquishment spectrum: any source at deadline `0` ⇒ poll;
finite min ⇒ timed wait; all `MEL_VAT_NEVER` and no ready work ⇒ block indefinitely. A source
holding undrained work must report deadline `0` until drained.

`mel_vat_run` drives turns until quit or nothing retains the vat (no open source, no ready
work). Nested runs are legal; `mel_vat_quit` stops the innermost run only.

## Invariants

- Cross-thread post publishes (mpsc push, release) before the doorbell rings; the driver drains
  the mailbox after every wake, so no wakeup is lost across the drain→block boundary (the
  kqueue doorbell is `EV_CLEAR` and latches while unparked).
- The doorbell coalesces: a surplus ring costs one empty wake (the kqueue and cocoa rings
  latch behind an atomic flag, cleared at wait entry, so awake-phase rings cost no syscall).
- The vat is parked whenever the driver is not mid-turn: the fair driver clears `parked` at
  turn entry and restores it (then re-drains and rings if ready work surfaced) before
  relinquishing, so a post or demand change landing while a subordinate host owns the CPU
  still reaches the waiter; owner-side posts and demand changes ring when the vat is at rest.
- Structural edits during a drain defer to the turn boundary (`closing` + reap).
- The vat borrows its waiter and driver; close them after the vat, sources before it.

## Owed (MEL-ENGINE-VIII)

- `demand_changed` re-arms but does not diff: a source's wakeable set membership must be stable
  for its life (events/deadline may vary freely). Set-diffing comes with the epoll/IOCP wave.
- Timer cancellation handles; today an added timer fires or dies with the timers source.
- Wakeable bridging for subordinate vats: the guest waiter refuses `arm`, so an fd source on a
  guest vat asserts; epoll/io_uring/IOCP waiters, the unfair driver.
- `vat/vsync.h` ships only the macOS CVDisplayLink source (`macos/src/vsync.c`: atomic
  signaled flag set on the CV thread, doorbell ring, deadline `0` when signaled else `NEVER`);
  CADisplayLink (ios), DXGI waitable swapchain (win32) and `requestAnimationFrame` (web)
  flavors are owed. Close stops the link before freeing, but `CVDisplayLinkStop` does not
  join an in-flight callback; the teardown window is inherited from the probe.
- Teardown hook so a vat-closed-first source can release its state; today order is contractual.

Deps: core, allocator, collection, executor, thread, time.
