# port

Proactor surface for the async substrate: submit an OS async I/O op on a file
descriptor, get a `Mel_Future` that resolves with `{bytes_transferred, status}`
delivered on a target executor (default the reactor's executor). The completion
side of the readiness/completion duality; the readiness side is `reactor`.

deps: core, allocator, collection, executor, future, reactor.
namespace: `<port/...>`, prefix `mel_port_`.

## Surface

    Mel_Port*   mel_port_create(.reactor = r, .alloc = a);
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
*before* `EPIPE` can be returned. The apple backend suppresses it two ways: it
sets `F_SETNOSIGPIPE` on the fd at submit (effective when the peer is still alive
at submit time, which is the common flow), and — the bulletproof guard, also
covering the peer-already-dead-at-submit case and pipes — it brackets the actual
`write`/`pwrite` syscall by blocking `SIGPIPE` on the loop thread, then draining
any `SIGPIPE` that became pending (`sigwait`) before restoring the mask. The op
then resolves `ERROR | MEL_PORT_PEER_CLOSE`; the process survives.

## Ownership

The future is embedded in the op record; one allocation per op. The consumer
owns the future once returned: read `mel_port_future_result()` in its `then`
continuation, then `mel_port_future_release()` to free the record. The reactor
source that drives the op is a separate, reactor-owned allocation (the reactor's
dispose ordering frees the source after its finalize runs, so the op record
cannot also be that source). Cancellation and port teardown resolve the future
cancelled; the consumer still releases it.

`Mel_Port_Op` is a generation-checked handle (slotmap), so a `cancel` of an op
that already completed is a safe no-op returning `false`, not a use-after-free.

## Thread affinity (loop-thread-only)

`mel_port_read`, `mel_port_write`, `mel_port_cancel` and `mel_port_destroy` are
**loop-thread-only** and assert `mel_reactor_is_owner(reactor)` up front (loud,
MEL-ENGINE-VIII). This is inherited from the reactor: its contract (README) is
that *every source and poll mutation is unsynchronised and must happen on the
loop thread*. Each of these calls mutates the reactor source list
(attach/destroy) and the port's plain slotmap, and the op-record's
`settled`/`detached` flags are plain (only the embedded future is CAS-guarded).
Because completion also runs on the loop thread, an on-loop cancel is serialized
against an on-loop completion — the future's one-shot CAS settles once and the
slotmap-remove + source-detach run exactly once with nothing racing them. Off-loop
they would race the slotmap and the reactor source list and could double-settle
the record; the assert turns that latent corruption into an immediate failure.

To act on a port from another thread, marshal onto the loop. For cancel, capture
the `Mel_Port_Op` and a `Mel_Port*`, then either:

    mel_reactor_post(reactor, do_cancel, &cell);   // cell = {port, op}; runs on loop

or embed a `Mel_Task` and `mel_reactor_defer` it; its `run` issues the
`mel_port_cancel` on the loop thread. The same applies to submitting reads/writes
from a worker: hand the request to the loop and submit there. (`mel_reactor_quit`
is the one reactor entry that is genuinely any-thread; the port has none.)

## Two modes

A proactor reaches completion two ways:

1. **Reactor-source-integrated completion** (this backend, macOS). Readiness
   platforms have no native completion queue, so the op registers its fd with
   the reactor as a one-poll source. On the loop turn the fd reports ready, the
   source's dispatch performs the non-blocking `read`/`write` to completion and
   resolves the future — readiness wrapped up to completion, on the loop thread,
   with **no worker hop**. An empty port holds no source and costs nothing; a
   pending op blocks the loop until its fd is ready, never busy-spins
   (MEL-ENGINE-III). This is the zero-thread-hop fast path the substrate names
   for io_uring, kqueue, and GCD.

2. **Native completion / thread-drain fallback** (shaped, not built). Where the
   OS owns a completion queue:
   - **io_uring** (Linux): submit SQEs, drain CQEs. The CQ eventfd registers as
     one reactor poll, so completions are observed on the loop turn — still mode
     1's zero-hop shape, just native completion instead of readiness-to-I/O.
   - **IOCP** (Windows): `GetQueuedCompletionStatus`. Open question
     (design §Open questions) is how far GQCS-with-zero-timeout integrates on the
     loop thread before a dedicated drain thread is forced; the fallback is an
     own-thread drain that cross-submits the resolved future to the target
     executor via the same `future`/executor waist this backend already uses.

The internal seam (`port/backend.h`: `available`, `port_init`/`teardown`,
`submit`/`retract`) is what a future backend implements. The completion machinery
itself is never re-derived per backend: every backend resolves the same embedded
`Mel_Future` via `mel_port__op_settle`, which delivers through the future →
executor waist. A backend supplies only "make the bytes move and tell me the
count"; the substrate owns resolve + deliver.

## Platform selection

`src/apple/port_backend.inl` is the macOS/iOS backend, compiled into `port.c`
under `MEL_PLATFORM_APPLE`. Every other platform compiles
`src/none/port_backend.inl`, whose `available()` is `false` and whose `submit`
resolves the future `MEL_PORT_ERROR | MEL_PORT_UNAVAILABLE` — an honest, loud
"no backend here yet", never a dead stub pretending to work. io_uring, IOCP, and
the Linux/Windows backends are described above and land as new `.inl` files; the
core and the seam do not change.

## Tests

`port-loop` drives a real threaded reactor (`mel_reactor_spawn`): read/write on
a pipe and a socketpair complete with the correct byte count and data, the
continuation runs on the loop thread on a later turn (proving the no-worker-hop
deferred delivery), short read reports `PARTIAL`, EOF/peer-close surfaces in
status, a bad fd surfaces `BAD_FD`, a write to a closed peer (send buffer filled,
then peer closed) resolves `PEER_CLOSE` with the process surviving (SIGPIPE
suppressed), cancellation resolves cancelled and never fires again, a stale
handle cancel returns `false`, destroying the port with a pending/hung op resolves
it cancelled without crashing, many concurrent reads all complete, and an idle
port adds no busy-spin (a witness timer at a fixed cadence fires its expected
count with no pending ops). Every op in the suite is submitted, cancelled and the
port destroyed **on the loop thread** (the affinity contract above); the port is
created and torn down inside the loop, never after `mel_reactor_spawn` returns
(which, in THREADED mode, has already freed the reactor). Under that affinity the
suite is ThreadSanitizer-clean (0 races). Off-loop access asserts (see Thread
affinity); the suite does not exercise it, so this clean run does not certify
cross-thread use — there is none.

## Known gap: pure-POLLHUP write starvation (reactor debt)

Under the THREADED macOS reactor backend the only readiness signal the port sees
is the CFFileDescriptor read/write callback, which is fed by kqueue
EVFILT_READ/EVFILT_WRITE. `POLLHUP`/`POLLERR` are never written into the poll's
`revents` in THREADED mode (only ATTACHED mode runs an explicit `poll()` that
captures them). So an op whose fd reaches a state that reports a *pure* `POLLHUP`
with no read/write filter event — concretely, a `write` op submitted on a fd
whose peer is *already* closed with an empty send buffer, so EVFILT_WRITE never
fires — **hangs: no completion, no timeout**. (Once the send buffer has data
queued, the peer closing *does* deliver an EVFILT_WRITE EV_EOF and the op
completes `PEER_CLOSE` — this is the case the closed-peer test exercises and it
passes today.) The port already dispatches on `POLLHUP`/`POLLERR` in `revents`,
so the pure-HUP case is not fixable within `modules/port`; the THREADED reactor
backend must surface HUP/ERR readiness (its own `poll()` or EV_EOF translation).
Tracked as reactor debt. The closed-peer test guards itself with a bounded turn
budget and `MEL_SKIP`s (rather than hangs) if the op does not complete, so it
upgrades to a hard pass once the reactor surfaces HUP.
