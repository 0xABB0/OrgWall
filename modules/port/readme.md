# port

Proactor surface for the async substrate: submit an OS async I/O op on a file
descriptor, get a `Mel_Future` that resolves with `{bytes_transferred, status}`
delivered on a target executor (default `mel_vat_executor(vat)`). The completion
side of the readiness/completion duality; the readiness side is the vat's
waiter (`vat`).

deps: core, allocator, collection, executor, future, vat, log.
namespace: `<port/...>`, prefix `mel_port_`.

## Surface

    Mel_Port*   mel_port_create(.vat = v, .alloc = a);
    void        mel_port_destroy(Mel_Port*);
    bool        mel_port_available(const Mel_Port*);

    Mel_Future* mel_port_read (port, .fd, .buffer, .len, .offset?, .deliver?, .out_op?);
    Mel_Future* mel_port_write(port, .fd, .buffer, .len, .offset?, .deliver?, .out_op?);
    bool        mel_port_cancel(port, Mel_Port_Op);

    const Mel_Port_Result* mel_port_future_result(Mel_Future*);
    void                   mel_port_future_release(Mel_Future*);

The op kind is the function you called, never a tag (no enum dispatch). `read`
fills until first available bytes (POSIX `read` semantics: a short read
completes with `MEL_PORT_PARTIAL`, EOF/peer-close with `MEL_PORT_EOF`). `write`
drains the whole buffer across as many readiness turns as it takes, then
completes. `.offset >= 0` selects positional `pread`/`pwrite`; the default
`MEL_PORT_NO_OFFSET` uses the stream cursor.

`status` is severity (`MEL_PORT_OK`/`WARNED`/`ERROR`) plus a bitset
(`EOF`, `PEER_CLOSE`, `PARTIAL`, `BAD_FD`, `UNAVAILABLE`, `CANCELLED`) — never an
enum. Errors always surface in `status` and `os_error`; nothing fails silently
(MEL-ENGINE-VIII). The future carries only severity + `PARTIAL`; the full bitset
and the raw `errno` live in `Mel_Port_Result`.

A `write` to a closed-peer fd would, by default, raise the fatal `SIGPIPE`
*before* `EPIPE` can be returned. The posix backends suppress it (apple
additionally sets `F_SETNOSIGPIPE` at submit; the posix backend relies on the
guard alone) by bracketing the actual `write`/`pwrite` syscall: block `SIGPIPE`
on the loop thread, do the write, then drain any `SIGPIPE` that became pending
(`sigwait`) before restoring the mask. The op resolves `ERROR | PEER_CLOSE`
(`EPIPE`/`ECONNRESET`); the process survives. On win32 the failure surfaces as a
mapped `GetLastError`, no signal involved.

## Ownership

The future is embedded in the op record; one allocation per op. The consumer
owns the future once returned: read `mel_port_future_result()` in its `then`
continuation, then `mel_port_future_release()` to free the record. The op record
carries a `Mel_Vat_Wakeable` and rides its own four-entry vat source (deadline
NULL, drain = the readiness step), opened at submit and closed at settle.
Cancellation and port teardown resolve the future cancelled; the consumer still
releases it.

`Mel_Port_Op` is a generation-checked handle (slotmap), so a `cancel` of an op
that already completed is a safe no-op returning `false`, not a use-after-free.

## Thread affinity (loop-thread-only)

`mel_port_read`, `mel_port_write`, `mel_port_cancel` and `mel_port_destroy` are
**loop-thread-only** and assert `mel_vat_is_owner(vat)` up front (loud,
MEL-ENGINE-VIII). This follows the vat's contract: *every source open/close and
wakeable mutation is unsynchronised and must happen on the owner thread*. Each
of these calls opens/closes vat sources and mutates the port's plain slotmap,
and the op-record's `settled`/`detached` flags are plain (only the embedded
future is CAS-guarded). Because completion also runs on the loop thread, an
on-loop cancel is serialized against an on-loop completion — the future's
one-shot CAS settles once and the slotmap-remove + source-close run exactly once
with nothing racing them. Off-loop they would race the slotmap and the vat's
source list and could double-settle the record; the assert turns that latent
corruption into an immediate failure.

To act on a port from another thread, marshal onto the vat: embed a `Mel_Task`
and `mel_vat_post(vat, &task)` it; its `run` issues the `mel_port_cancel` on the
loop thread. The same applies to submitting reads/writes from a worker: hand the
request to the loop and submit there. (`mel_vat_post` and `mel_vat_quit` are the
vat entries that are genuinely any-thread; the port has none.)

## Two modes

A proactor reaches completion two ways:

1. **Readiness-wrapped-to-completion** (apple/posix). Readiness platforms have
   no native completion queue, so the op arms its fd as a `Mel_Vat_Wakeable` on
   a per-op vat source. On the loop turn the fd reports ready, the source's
   drain performs the non-blocking `read`/`write` to completion and resolves the
   future — readiness wrapped up to completion, on the loop thread, with **no
   worker hop**. An empty port holds no source and costs nothing; a pending op
   blocks the loop until its fd is ready, never busy-spins (MEL-ENGINE-III).

2. **Native completion** (win32, owed). Windows owns the completion (overlapped
   `ReadFile`/`WriteFile` harvested at the loop turn); that backend returns with
   the vat's IOCP waiter wave. Until then win32 compiles the `unavailable` stub
   (see *Windows*).

The internal seam (`port/backend.h`: `available`, `port_init`/`teardown`,
`submit`/`retract`) is what each backend implements. The completion machinery
itself is never re-derived per backend: every backend resolves the same embedded
`Mel_Future` via `mel_port__op_settle`, which delivers through the future →
executor waist, uses the same generation-checked op-record lifecycle, and honors
the same loop-thread affinity. A backend supplies only "make the bytes move and
tell me the count"; the substrate owns resolve + deliver.

## Platform selection

`build.c` selects exactly one backend translation unit per platform with
`WHEN(.platforms = MEL_ON(...))`, mirroring how other modules pick
`src/<platform>/`:

- **macOS / iOS** → `src/apple/port_backend.c` — vat-source readiness (the io
  waiter's kqueue, or the ui waiter's CFFileDescriptor bridge),
  `F_SETNOSIGPIPE` + the SIGPIPE-guarded write.
- **Linux / Android** → `src/posix/port_backend.c` — vat-source readiness
  against the vat wakeable surface (IN/OUT/ERR/HUP), SIGPIPE-guarded write; a
  Linux/Android vat waiter (epoll/ALooper) is owed by the vat's epoll wave. See
  *Linux readiness* below for why epoll/io_uring resolve to this one TU.
- **win32** → `src/win32/port_backend.c` — the `unavailable` stub: the vat has
  no win32 waiter to poll OVERLAPPED events with yet (see *Windows*).
- **wasm** → `src/none/port_backend.c` — `available()==false` (see *wasm*).

`src/none/port_backend.c` is an honest, loud "no proactor here": `available()`
is `false` and `submit` resolves `MEL_PORT_ERROR | MEL_PORT_UNAVAILABLE`, never a
dead stub pretending to work.

## Linux readiness: why one posix TU, not a separate io_uring/epoll engine

The Linux/Android backend does not open its own epoll/io_uring fd. It rides the
**vat's waiter**, which owns the readiness wait. The port arms the op's fd as a
vat wakeable on a per-op source and does the non-blocking syscall on readiness —
identical in shape to the macOS kqueue path, and zero-cost when idle. This is
deliberate:

- **io_uring** was assessed: `liburing.h` is absent from the cross sysroot;
  `linux/io_uring.h` and `__NR_io_uring_setup`/`_enter` are present, so a raw-
  syscall ring is *buildable* but would mean hand-managing mmap'd SQ/CQ rings,
  runtime kernel-version detection (≥5.1), and a parallel completion path that
  cannot be runtime-verified in this environment — exactly the kind of unproven
  complexity the commandments forbid shipping. The design names io_uring as the
  zero-hop ideal; when it lands it slots in as a new TU behind the same
  `backend.h` seam (its CQ eventfd registers as one vat wakeable, preserving the
  loop-turn completion shape) with no change to the core. Until then, epoll/poll
  readiness-to-completion is the safe portable choice the design explicitly
  sanctions; the vat wakeable surface carries `POLLHUP`/`POLLERR` as
  `MEL_VAT_WAKE_HUP`/`ERR`.

## Windows (win32): unavailable stub, pending the IOCP waiter

`src/win32/port_backend.c` is the `unavailable` stub: `available()` is `false`
and every submit resolves `MEL_PORT_ERROR | MEL_PORT_UNAVAILABLE` — loud, not
silent (MEL-ENGINE-VIII). The old reactor-era backend polled OVERLAPPED event
handles through the reactor's wait set; the vat has no win32 waiter to poll them
with yet. The native backend returns with the vat's IOCP waiter wave: each op
issues an overlapped `ReadFile`/`WriteFile` and the loop turn harvests with
`GetOverlappedResult` — completion observed on the loop thread, no drain thread
(MEL-ENGINE-III). git holds the old backend's error mapping and
`FILE_FLAG_OVERLAPPED` constraints as the porting reference.

## wasm: no proactor surface (available()==false)

A browser/WASI sandbox has no generic file-descriptor async I/O: there is no
`read`/`write` proactor over arbitrary fds to wrap. Faking a surface would be a
silent lie (MEL-ENGINE-VIII). So wasm compiles `src/none/port_backend.c`:
`mel_port_available()` returns `false` and any submit resolves
`MEL_PORT_ERROR | MEL_PORT_UNAVAILABLE`. A real wasm async story (Asyncify-driven
`fetch`, OPFS, or WASI poll_oneoff) is a *different* surface than fd read/write
and would be its own backend behind the same seam if and when it is specified.

## Tests

`port-loop` opens a vat on the test thread (io waiter + fair driver, a
deadline-0 idle source driving the scenario bodies): read/write on a pipe and a
socketpair complete with the correct byte count and data, the continuation runs
on the loop thread on a later turn (proving the no-worker-hop deferred
delivery), short read reports `PARTIAL`, EOF/peer-close surfaces in status, a
bad fd surfaces `BAD_FD`, a write to a closed peer (send buffer filled, then
peer closed) resolves `PEER_CLOSE` with the process surviving (SIGPIPE
suppressed), cancellation resolves cancelled and never fires again, a stale
handle cancel returns `false`, destroying the port with a pending/hung op
resolves it cancelled without crashing, many concurrent reads all complete, and
an idle port adds no busy-spin (a witness timer at a fixed cadence fires its
expected count with no pending ops, validating that vat timed waits actually
sleep). Every op in the suite is submitted, cancelled and the port destroyed
**on the loop thread** (the affinity contract above); the vat outlives the port
(`mel_vat_close` runs after `mel_port_destroy`). Off-loop access asserts (see
Thread affinity); the suite does not exercise it, so a clean run does not
certify cross-thread use — there is none.

The tests use pipes/socketpairs (POSIX), so they compile and link on
macOS / Linux / Android (`./nob build port-loop <p>`); they **run** on macOS here
(the only host) and pass 17/17. win32 is the `unavailable` stub and wasm has no
surface to test (`available()==false`).

The `test/tsan_*.c` drivers are standalone ThreadSanitizer harnesses built by
`test/tsan_build.sh <driver.c>` (they need every TU compiled
`-fsanitize=thread`, which the nob target graph does not express per-target):
`tsan_loop_cancel` races on-loop cancel against completion across 64 ops
(passes, tsan-clean); `tsan_kqprobe` demonstrates the raw kqueue
`EVFILT_WRITE` starvation after peer `shutdown()`; `tsan_pollhup` reproduces
the resulting port hang (see *Known gap*). `tsan_cancel_race` predates the
affinity contract — it cancels from foreign threads, which now trips the
`mel_vat_is_owner` assert by design; it documents exactly the corruption the
assert forbids and fires only if that guard regresses.

## Known gap: pure-HUP write starvation (vat waiter debt)

The vat wakeable surface carries `MEL_VAT_WAKE_HUP` and the kqueue waiter
translates `EV_EOF` into it, so a peer that **closes** its fd completes a
pending write `PEER_CLOSE` — the closed-peer test passes hard (no skip) on the
io waiter today. But a peer that **shuts down without closing**
(`shutdown(SHUT_RDWR)`, fd kept open) fires no `EVFILT_WRITE` event and no
`EV_EOF` at all — a write op pending on a full send buffer **hangs: no
completion, no timeout**. `test/tsan_kqprobe.c` demonstrates the raw kqueue
starvation and `test/tsan_pollhup.c` reproduces the hang through the port
(exit 1, `POLLHUP-HANG-REPRODUCED`). Not fixable within `modules/port` — the
waiter must synthesize the HUP edge (an `EVFILT_READ` companion filter, or a
poll-mode probe). The cocoa/ui waiter's CFFileDescriptor bridge is weaker
still: it can only express read/write callbacks, never HUP. The closed-peer
test keeps its bounded turn budget and `MEL_SKIP`s rather than hangs if a
waiter fails to surface the edge.
