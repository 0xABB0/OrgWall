# Async-First Architecture

## The decision

Melody is **async-first at every layer where async makes sense**. Any API whose latency is dominated by waiting on something outside the calling thread is async by default. CPU-bound APIs stay synchronous.

This is a substrate-tier decision. It dictates the shape of `runtime/`, every `os/*` module, every `net/*` module, every `db/*` module, parts of `serial/`, `compress/`, `crypto/`, and how domain modules (`gui/`, `audio/`, `gpu/`) integrate.

## What "async-first" actually means

The sharpened rule: **anything that waits on the kernel, the network, the filesystem, a timer, another task, or a GPU fence is async by default. Pure CPU work is not.**

What is async:
- File and filesystem I/O
- Network I/O
- Process I/O (stdin/stdout, wait)
- IPC, signals
- DB queries
- Timer sleeps
- Inter-task waits
- GPU fence waits

What is *not* async (making it async is a category error):
- Math, hashing, string manipulation
- In-memory data-structure ops
- Allocator calls
- Pure parsing of buffers already in RAM

What has a *different* concurrency model and should not be forced into async:
- **Audio realtime callback** — hard-realtime, fixed deadline, no allocations, no syscalls, no waiting. Lives in its own world, fed via lock-free SPSC queues from the async side.
- **GUI main thread** — OS-imposed thread pinning. The GUI event loop *is* a reactor; integrate it as one.

## The four real costs

1. **Library becomes framework.** Anything using async needs the runtime running. Users don't opt out. Acceptable trade for Melody (personal substrate), but it is a real shift from "drop-in stdlib" to "platform you build on".

2. **Two schedulers, not one.** A reactor (for I/O completions) and a worker pool (for CPU work). If you put a JSON parse on the I/O thread, you've stalled every socket on that reactor. `runtime/scheduler` (the job system) and `runtime/reactor` (the I/O event loop) cooperate but are distinct.

3. **Cancellation must be designed in from day one.** Bolted-on cancellation is the #1 way async libraries become unusable. Every awaitable must respond to a cancel token, propagate cancellation to children, and unwind buffers/handles correctly. Decide cancel semantics *first*.

4. **Buffer ownership across the suspension point.** With io_uring/IOCP, when an op is submitted, the kernel owns the buffer until completion. If the task is canceled, the buffer cannot be freed until the kernel hands it back. This is the bug that kills naive async-first designs in C. The API must make ownership unambiguous.

## Architectural shape

### `runtime/fiber` — the substrate

Already exists. Stackful fibers are **the right choice for C async-first**, and the single biggest enabler. Stackful means an `await(...)` can happen at any call depth, and the calling helper looks synchronous. Stackless coroutines force function coloring up the call graph — viral, effectively unworkable in C-with-macros for general-purpose code. See `fiber-and-coroutine.md`.

### `runtime/reactor` — the I/O event loop

One per thread, or a shared one (see threading model below). Wraps `io_uring` (Linux), IOCP (Windows), `kqueue` (BSD/macOS). Owns the completion queue. The reactor's job: when a submitted operation finishes, resume the fiber waiting on it.

### `runtime/scheduler` — the executor

Already exists as `async/job`. Runs ready fibers and CPU jobs. The reactor delivers completions into the scheduler's ready queue.

### `runtime/timer` — the timer wheel

A "sleep for N ms" is a fiber yielding on a timer entry; the wheel fires, the fiber goes back into the ready queue.

### `runtime/task` — the user-facing handle

Spawn a fiber doing work, get a `Mel_Task*`. Operations: `await`, `cancel`, `join`, `detach`. Generalizes over fibers + worker-pool jobs (some tasks live on the reactor thread, some on the CPU pool — the user mostly doesn't care).

### `runtime/cancel` — cancellation tokens

Cooperative. Composable: cancel-on-deadline, cancel-on-any-of, cancel-on-parent. Every awaiter checks the token before suspending and on resume.

### `runtime/buffer` — owned buffers with in-flight state

Owned buffers with explicit "in-flight" tracking. While an I/O op holds it, the user cannot touch or free it. The boring piece nobody likes designing and everybody regrets skipping.

## Threading model

Pick one and commit. The choice is binding.

**Per-thread reactors, share-nothing** *(recommended for Melody)*
Each thread has its own reactor + scheduler. Fibers do not migrate across threads. Cross-thread communication is explicit, via `runtime/channel`.
- Pros: simple, fast, no atomic-heavy task plumbing, pairs naturally with fibers (no TLS migration nightmare).
- Cons: less load-balancing flexibility.
- Used by: Seastar, glommio, most game engines.

**Shared work-stealing pool**
Tasks can be picked up by any thread.
- Pros: better load balancing, more flexible.
- Cons: atomic ops everywhere, harder to reason about, fiber migration across threads is genuinely nasty in C (TLS, stack-residency assumptions).
- Used by: Tokio, Go.

Melody's pick: **per-thread reactors, share-nothing**, with a thread-safe channel layer for crossing threads.

## Module impact, async-first lens

Marker key: **A** = async surface, **S** = sync, **R** = runtime substrate, **M** = mixed/special-case.

### Async by default (returns `Mel_Task*` / awaitable)

- **A** `os/file` — open/read/write/seek/close/stat
- **A** `os/fs` — readdir, watch, rename, remove
- **A** `os/io` — the reactor surface
- **A** `os/process` — wait, stdin/stdout streams
- **A** `os/signal` — wait-for-signal
- **A** `os/ipc` — pipes, UDS, shm-with-eventfd
- **A** `net/socket`, `net/dns`, `net/tls`, `net/http`, `net/ws` (every read/write/accept/connect)
- **A** `db/*` — every query
- **A** `log/*` — sink writes (producer never blocks)
- **A** `telemetry/*` — span exporters, metric pushes
- **A** `serial/*` *streaming variants* — `json_parse_stream(reader)` where reader is async
- **A** `script/*` — usually async to integrate with the runtime

### Sync, no exceptions (pure CPU; async would be malpractice)

- **S** `core`, `types`, `math`, `hash` (non-crypto), `string`, `collection`, `allocator`, `encoding`
- **S** `serial/*` *in-memory variants* — parsing a `Mel_Str8` already in RAM
- **S** `crypto/*` for short inputs (async wrapper via offload pool for long ops)
- **S** `compress/*` (async wrapper via offload pool)
- **S** `rand/*`

### Different concurrency model

- **M** `gui/*` — reactor-integrated. The OS GUI event loop *is* a reactor. Must run on the OS-mandated thread (typically main).
- **M** `audio/*` realtime path — hard-realtime callback. No fibers, no reactors, no waiting. Lock-free SPSC queues bridge to the async control side.
- **M** `gpu/*` — command record is sync (record + enqueue). Waiting on GPU is async via fences integrated into the reactor (Vulkan timeline semaphores, D3D12 fences, Metal events). Do not model GPU work as fibers.
- **M** `video/*` decode — CPU-heavy with realtime-ish deadlines. Worker pool with priority, not a reactor.
- **M** `image/*` decode — sync core, async wrappers via offload pool.

## Non-obvious gotchas

These will bite. Plan for them.

- **Not every blocking syscall has an async sibling.** `getaddrinfo`, some filesystem ops on macOS, `dlopen`, fork-exec on some platforms. Solution: a **blocking pool** — a small thread pool that runs blocking syscalls and reports completion to the reactor. Tokio calls this `spawn_blocking`. Plan for it; you will need it.

- **TLS doesn't follow fibers.** If fibers migrate threads (or even if they don't, but code caches `pthread_self`-derived state), TLS bites. Per-thread-reactor model avoids the worst of this. Even so: ban code from caching TLS across yield points.

- **Stackful coro stacks are not free.** Default 64KB × thousands of fibers = real memory. Mitigations: small default stacks (8–16KB) with guard pages; commit-on-demand via virtual memory (`allocator.vmem` is the substrate). The cost only matters at hundreds-of-thousands-of-connections scale.

- **Cancellation must be checkpointed.** A fiber in a long CPU loop won't honor cancellation unless it yields. Options: define cancellation points explicitly; insert periodic `mel_coro_yield()` in long CPU paths; hard-cancel via fiber unwinding (extremely annoying in C — no destructors). Pick a convention and document it.

- **Async log + crash = lost log.** If log writes are async and the process crashes, the last N events vanish. Solution: synchronous flush on the panic path, or a ring-in-shm that an out-of-process collector reads.

- **GUI main-thread tyranny.** macOS/Windows insist UI runs on the launching thread. Reactor design must allow "this reactor must run on thread T". Don't assume reactors are fungible.

- **Hot reload + async.** Hot-reloading a module with in-flight tasks invalidates the function pointers backing those fibers. Hot-reload needs a quiesce-and-drain protocol with cancellation. Plan it before plugins exist, not after.

## Build order

Given the current starting point (have: fiber, coroutine, job; missing: reactor, timer, cancel, sync, buffer):

1. **`error`** — every async API returns errors; nail the model first.
2. **`runtime/sync`** + **`runtime/thread`** — mutex/condvar/atomics + OS thread wrappers. Trivial, unblocks everything.
3. **`runtime/cancel`** + **`runtime/buffer`** — the pieces that hurt later if skipped.
4. **`runtime/timer`** — small, but the reactor needs it.
5. **`runtime/reactor`** — the big one. Start with one backend (io_uring on Linux, or kqueue on macOS — whichever the primary dev platform is). Add others later.
6. **`runtime/task`** — unify "fiber-awaiting-a-completion" and "job-on-worker" under one handle.
7. **`os/file`** as the *first* concrete async API — proves the stack end-to-end. If file read works cleanly, the design is sound.
8. **`os/fs`** — second proof.
9. **`net/socket`** — third proof.
10. Everything else is grinding it out: dns → tls → http → db → encoding → serial → telemetry → gui-integration → audio-integration → gpu-integration → script → ...

The reorg `async/* → runtime/*` happens **before** step 2. Otherwise the names lie.

## One last constraint

**Do not try to make every domain module async-first in one pass.** The substrate is one project. Wiring each domain module through it is another project per module. A half-finished async-first GUI is worse than a sync GUI. A half-finished async-first db driver is worse than a sync one. **Substrate first, prove it on file/fs/net, then propagate one domain at a time.**

See also: `runtime.md`, `modules-overview.md`, `fiber-and-coroutine.md`.
