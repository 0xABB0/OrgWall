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

1. **Readiness-wrapped-to-completion** (apple/posix). Readiness platforms have
   no native completion queue, so the op registers its fd with the reactor as a
   one-poll source. On the loop turn the fd reports ready, the source's dispatch
   performs the non-blocking `read`/`write` to completion and resolves the future
   — readiness wrapped up to completion, on the loop thread, with **no worker
   hop**. An empty port holds no source and costs nothing; a pending op blocks
   the loop until its fd is ready, never busy-spins (MEL-ENGINE-III).

2. **Native completion** (win32). Windows owns the completion: the op issues an
   overlapped `ReadFile`/`WriteFile`, and the loop harvests with
   `GetOverlappedResult` when the I/O completes — still mode 1's zero-thread-hop
   shape (the completion is observed on the loop turn, no drain thread), just
   native completion instead of readiness-to-I/O.

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

- **macOS / iOS** → `src/apple/port_backend.c` — reactor-source readiness
  (kqueue via the reactor), `F_SETNOSIGPIPE` + the SIGPIPE-guarded write.
- **Linux / Android** → `src/posix/port_backend.c` — reactor-source readiness
  (Linux: the reactor's `poll()`; Android: the reactor's `ALooper`, both of
  which already surface `POLLHUP`/`POLLERR`), SIGPIPE-guarded write. See
  *Linux readiness* below for why epoll/io_uring resolve to this one TU.
- **win32** → `src/win32/port_backend.c` — overlapped I/O + `GetOverlappedResult`
  (see *Windows*).
- **wasm** → `src/none/port_backend.c` — `available()==false` (see *wasm*).

`src/none/port_backend.c` is an honest, loud "no proactor here": `available()`
is `false` and `submit` resolves `MEL_PORT_ERROR | MEL_PORT_UNAVAILABLE`, never a
dead stub pretending to work.

## Linux readiness: why one posix TU, not a separate io_uring/epoll engine

The Linux/Android backend does not open its own epoll/io_uring fd. It rides the
**reactor**, which already owns the readiness wait (Linux: `poll()`; Android:
`ALooper`). The port registers the op's fd as a one-poll reactor source and does
the non-blocking syscall on readiness — identical in shape to the macOS kqueue
path, and zero-cost when idle. This is deliberate:

- **io_uring** was assessed: `liburing.h` is absent from the cross sysroot;
  `linux/io_uring.h` and `__NR_io_uring_setup`/`_enter` are present, so a raw-
  syscall ring is *buildable* but would mean hand-managing mmap'd SQ/CQ rings,
  runtime kernel-version detection (≥5.1), and a parallel completion path that
  cannot be runtime-verified in this environment — exactly the kind of unproven
  complexity the commandments forbid shipping. The design names io_uring as the
  zero-hop ideal; when it lands it slots in as a new TU behind the same
  `backend.h` seam (its CQ eventfd registers as one reactor poll, preserving the
  loop-turn completion shape) with no change to the core. Until then, epoll/poll
  readiness-to-completion is the safe portable choice the design explicitly
  sanctions — and on Linux the reactor surfaces `POLLHUP`/`POLLERR`, so the
  pure-HUP starvation that afflicts the macOS THREADED path does not occur here.

## Windows (win32): overlapped I/O, reactor-integrated

The public surface is `i32 fd` (a CRT file descriptor); the backend converts it
with `_get_osfhandle`. Each op carries an `OVERLAPPED` and a manual-reset event:
it issues an overlapped `ReadFile`/`WriteFile`, and

- if the call completes synchronously (`TRUE`), the op settles immediately;
- if it returns `ERROR_IO_PENDING`, the op's event HANDLE is registered as the
  reactor poll. The win32 reactor waits on HANDLEs (`MsgWaitForMultipleObjects`);
  when the event signals, the loop turn dispatches and the op harvests the byte
  count with `GetOverlappedResult` and resolves the future — on the loop thread,
  no drain thread (MEL-ENGINE-III). `mel_port_cancel`/teardown call `CancelIoEx`.

Errors map to the status bitset (`ERROR_HANDLE_EOF`/`ERROR_BROKEN_PIPE` → `EOF`;
`ERROR_NETNAME_DELETED`/`ERROR_PIPE_NOT_CONNECTED` → `PEER_CLOSE`;
`ERROR_INVALID_HANDLE` → `BAD_FD`; `ERROR_OPERATION_ABORTED` → `CANCELLED`).

Constraints (Win32 facts, surfaced not faked): the fd's underlying HANDLE must be
opened `FILE_FLAG_OVERLAPPED` for true async; overlapped HANDLEs have no OS file
pointer, so stream-mode (`MEL_PORT_NO_OFFSET`) reads/writes on a *regular file*
start at offset 0 — pass an explicit `.offset` for files (pipes/sockets ignore
it, as on POSIX). Winsock `SOCKET`s are not CRT fds and are out of scope for the
`i32 fd` surface; file/pipe handles are covered.

**Verification:** the win32 backend is compile-clean against the mingw Win32
headers locally, but the repo's win32 toolchain is clang/MSVC on the remote
`win-pilot` box (no MSVC SDK here). It is therefore **remote-unverified**: it must
be built (`./nob build port win32`) and run on win-pilot after a branch push. The
code uses only standard Win32/CRT (`_get_osfhandle`, `CreateEventW`, overlapped
`ReadFile`/`WriteFile`, `GetOverlappedResult`, `CancelIoEx`).

## wasm: no proactor surface (available()==false)

A browser/WASI sandbox has no generic file-descriptor async I/O: there is no
`read`/`write` proactor over arbitrary fds to wrap. Faking a surface would be a
silent lie (MEL-ENGINE-VIII). So wasm compiles `src/none/port_backend.c`:
`mel_port_available()` returns `false` and any submit resolves
`MEL_PORT_ERROR | MEL_PORT_UNAVAILABLE`. A real wasm async story (Asyncify-driven
`fetch`, OPFS, or WASI poll_oneoff) is a *different* surface than fd read/write
and would be its own backend behind the same seam if and when it is specified.

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

The tests use pipes/socketpairs (POSIX), so they compile and link on
macOS / Linux / Android (`./nob build port-loop <p>`); they **run** on macOS here
(the only host) and pass 17/17. Linux/Android are compile-and-link verified
(same posix backend, same reactor source model that surfaces HUP/ERR), runtime to
be exercised on a Linux host. win32 is not covered by `port-loop` (the test is
POSIX-shaped); the win32 backend is built/run on win-pilot. wasm has no surface to
test (`available()==false`).

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
