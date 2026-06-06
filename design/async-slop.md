# Async Model — Design

The aim: every async pattern expressible in its own best, idiomatic backend, by composing a small set
of orthogonal primitives. A consumer links only the axes it needs; new patterns are new *combinations*,
not new modules (MEL-ENGINE-IX). The simple path and the powerful path are one path (MEL-ENGINE-II).

## The frame: a dataflow of readiness

Stripped to nothing, asynchrony is the propagation of *readiness*: something becomes ready → that fact
is routed to a context → a continuation runs there → it may make further things ready. Six separable
concerns follow. Each is an independent axis with pluggable backends, so total coverage is the **sum**
of per-axis backends, never their product.

1. **Detect** — what notices readiness. *(Demux, Source, Inbound adapter.)*
2. **Route** — the edge carrying "now ready." *(Waker.)*
3. **Run-context** — where the continuation executes; the affinity. *(Executor.)*
4. **Resume** — how a body pauses and resumes. *(Plain / fiber / continuation.)*
5. **Coordinate** — how values travel between tasks. *(Future / event / channel / counter / request-map.)*
6. **Bound** — cancellation, timeout, lifetime. *(Scope.)*

The load-bearing rule that makes the axes orthogonal: **the context that handles a readiness is a
parameter of the detection, not a property of the loop.** "Detect here, handle there" must be a
first-class statement. Everything below serves that.

## Route — `Waker`

The universal "make-ready" edge, one call:

```c
typedef struct { void (*wake)(void* user); void* user; } Mel_Waker;
```

`wake` is callable from any thread and any context (realtime, OS-callback, another core). It is
idempotent under coalescing and never blocks, allocates, or fails. Every Source, every completion,
every resolved coordination object fires a Waker; the Waker decides what readiness *means* in its
context — usually "submit a Task to an Executor," sometimes "set an SPSC flag the audio thread polls."

## The runnable atom — `Task`

```c
typedef struct Mel_Task Mel_Task;
struct Mel_Task {
    void (*run)(Mel_Task* self);
    Mel_Mpsc_Node link;
    _Atomic(i32) armed;
};
```

Intrusive: the owner (source record, future cell, fiber park node, continuation frame) embeds it and
recovers itself by `mel_container_of`. The embedded MPSC node makes enqueue on the wakeup path
**unbounded, zero-alloc, never-fail** (MEL-ENGINE-VIII). `armed` coalesces redundant wakes (CAS 0→1 to
enqueue; cleared before `run`).

## Run-context — `Executor`

```c
typedef struct Mel_Executor Mel_Executor;
struct Mel_Executor { void (*submit)(Mel_Executor* self, Mel_Task* task); };
```

Affinity is **which handle you hold**, enforced at the violation site, never a flag (MEL-CODE-001).
Executors are **instanceable**, not global singletons (a process may hold many). The kinds:

- **Inline** — runs on the calling thread through a thread-local trampoline drain; never recursive,
  never re-entrant mid-task. For "run it here, now, without growing the stack."
- **Serial** — a single owned thread or a loop thread; tasks run one at a time, ordered. The
  UI/main-thread executor, a device thread, a per-connection serializer.
- **Pool** — a work-stealing, fiber-backed worker set; tasks may park mid-flight on a counter and the
  worker steals other work (Chase–Lev per worker + MPMC handoff).
- **Dedicated / blocking** — an isolated thread (or small pool) for unavoidably-synchronous calls kept
  off every loop; resolves a future on a target executor.
- **Per-core** — one Serial-or-Pool executor pinned per core, share-nothing; cross-core delivery is an
  explicit Waker targeting that core's executor.
- **Realtime-constrained** — a *handle* over an OS-owned realtime thread that asserts on alloc / lock /
  syscall (MEL-ENGINE-VIII); the only legal body is continuation resume + SPSC transport.

Capability is the *type of handle*, so a new executor kind never touches the protocol (MEL-ENGINE-IV).

## Detect — `Demux`, `Source`, inbound adapter

The piece most prone to under-definition, made explicit here.

### Demux

A **Demux** owns a set of Sources and turns mechanism-specific readiness into Waker fires. It has two
co-equal integration modes, because some platforms let us own the loop and some own it themselves:

- **Driven** — *we* own the loop: `mel_demux_drive(d, timeout)` blocks until a source is ready or the
  deadline elapses, then fires wakers. Backends: epoll, io_uring, kqueue, IOCP, poll/select, a
  poll-thread, a timer-wheel.
- **Hosted** — the OS owns the loop: the Demux registers its handle(s) with the platform loop and is
  called back. Backends: CFRunLoop, ALooper, the browser event loop.

```c
typedef struct Mel_Demux Mel_Demux;
i64  mel_demux_drive(Mel_Demux* d, i64 timeout_ns);
void mel_demux_host (Mel_Demux* d, Mel_Host_Loop host);
```

`mel_demux_drive` blocks until a source is ready or the deadline elapses, then fires wakers (the driven
mode); `mel_demux_host` registers the demux's handle with the platform loop, which calls it back (the
hosted mode).

The Source API is identical across modes and backends; only the Demux backend differs. A loop is not a
primitive — it is the *bundle* `Serial executor + Demux + timer Source`.

### Source

A **Source** is a registration in a Demux that binds *{readiness condition} → {Waker} → {target
executor}*. The target executor is the keystone: the same readiness is handled in any context by
changing the parameter.

```c
typedef struct Mel_Source Mel_Source;
typedef struct {
    bool (*prepare)(Mel_Source* s, i64* deadline_ns);
    bool (*ready)(Mel_Source* s);
    void (*retract)(Mel_Source* s);
} Mel_Source_Vtable;
```

A Source carries a target executor and an on-ready waker; the vtable is its backend behaviour, and each
kind below is a distinct constructor over it, never a `kind` enum (MEL-CODE-001). Each kind has its own
idiomatic backend:

- **Readiness** — watch an fd for read/write/error; the handler does the I/O (epoll/kqueue/poll).
- **Completion** — an op record submitted to the OS; readiness *is* the result (io_uring/IOCP/aio).
- **Timer** — a deadline (timerfd/`EVFILT_TIMER`/waitable timer/computed timeout/timer-wheel).
- **Signal** — a POSIX signal (signalfd/`EVFILT_SIGNAL`).
- **User / self-wake** — cross-thread wakeup of a sleeping Driven demux (eventfd/`EVFILT_USER`/
  `PostQueuedCompletionStatus`/`CFRunLoopSource`/`ALooper_wake`/`postMessage`). This is how a `submit`
  from another thread breaks the loop's wait.
- **Poll** — a user predicate tested on a cadence (per-frame, or on a poll-thread), firing when it
  flips. Subsumes "poll a sensor / a lock-free queue / a fence value, here or on another thread."

A Source is retractable (`mel_source_retract`) with a hard guarantee: after retract returns, its Waker
will not fire again (generation-checked).

### Inbound adapter

The dual of a Source, for OS-**push** contexts where the OS calls *us* on a thread we do not drive:
window message pump, vsync/display-link, audio render, completions landing on OS-owned threads (IOCP
workers, GCD queues). The adapter captures the event and chooses, explicitly:

- **inline** — run the handler here under a constrained executor handle (realtime/main), or
- **handoff** — post a Task to a target executor.

The inline-or-handoff choice is named and visible, never a hidden default (MEL-CODE-007).

## Resume — suspension technique

Orthogonal to where a task runs and what wakes it; the executor is blind to which is used.

- **Plain** — no suspension; the body runs to completion in one `run`.
- **Fiber** — stackful machine-stack switch (asm per arch); suspends *anywhere*, through any helper
  depth. The general `await`. Cannot be snapshotted or run where stacks are forbidden. Parked/woken via
  a counter.
- **Continuation** — stackless reified frame (integer resume index, POD, caller-allocated). Resumes by
  plain call against caller memory. The only technique legal on the realtime thread; the only one that
  snapshots (save / rollback / replay) and survives hot-reload. Suspends only at marked points.
- **Web note:** the main thread admits no fiber block; stackful suspension there is JSPI/Asyncify, and
  continuation is always available.

## Coordinate — value topology

Built on Waker + Task; these are the user-facing API, resolving by firing wakers.

- **Future** — 1→1 one-shot value; CAS `pending → (resolved | cancelled)`; one continuation on a target
  executor; `when_all`/`when_any`. The reply to a request, the result of a completion op.
- **Event** — 1→N broadcast; refcounted membership snapshot; per-subscriber bounded FIFO with explicit
  loss policy (latest / lossy-lag / lossless / custom). Watch, pub/sub, input fan-out.
- **Channel** — M→N CSP stream; unbuffered rendezvous or bounded ring; `select`; blocking flavor (parks
  a worker fiber) and future flavor (callback world) over one channel. Pipelines, producer/consumer.
- **Counter / latch** — fiber-blocking dual of a future; wait for N things; barrier; the fork-join
  join. The same wait callback-side is a future.
- **Request-map** — correlate many in-flight requests to their replies (an id→future table), for
  request/response protocols.

Higher patterns are compositions, not primitives: debounce/throttle = timer Source + latest-value
event; retry-with-backoff = future + timer; deadline = timer Source raced against the op via `when_any`,
cancel the loser; scatter/gather = `when_all` over futures or a counter.

## Bound — `Scope`

Structured cancellation and lifetime, threaded through Tasks and Sources. A parent Scope cancels its
children — pending futures, registered Sources, submitted completion ops — and joins them **before**
their executor or demux frees (cancel precedes free, MEL-ENGINE-VIII). Timeout is a Scope bounded by a
timer Source. Graceful drain is a Scope that stops admitting and lets in-flight work finish.

## I/O surface

User-facing I/O is **completion-shaped**: submit an op, get a future that resolves with the result on a
target executor.

```c
Mel_Future* mel_io_read (Mel_Io* io, Mel_Io_Read_Opt opt);
Mel_Future* mel_io_write(Mel_Io* io, Mel_Io_Write_Opt opt);
bool        mel_io_cancel(Mel_Io* io, Mel_Io_Op op);
```

Underneath, the op is a Completion Source on a Demux whose backend is either **native completion**
(io_uring/IOCP — submit to the SQ, the CQ fires the waker, no thread hop where the queue drains on the
loop) or **readiness + a completion shim** (epoll/kqueue — the readiness Source fires, the shim does the
non-blocking syscall to EAGAIN and synthesizes the completion).

Raw **readiness / timer / signal Sources stay first-class and exposed** — for non-I/O fds, custom
protocols, GPU fence values, and the cases where the user wants to own the syscall.

## Concurrency topologies

The instanced Executor + Demux supports both, and they compose:

- **Shared work-stealing pool** — one Pool executor + the loop bundle. Games, GUI, CLI, modest servers.
- **Thread-per-core / share-nothing** — N bundles (Serial executor + own Demux) pinned per core, no
  shared mutable state; connections sharded by core; cross-core work is an explicit Waker targeting the
  destination core's executor, carried over an SPSC/MPSC. Server tail-latency and NUMA scaling
  (MEL-ENGINE-VI).

A Completion Source on core A delivers to core A's executor by default; a result needed on the loop
thread is a Waker targeting the loop executor — the same routing, a different target.

## Interfaces — a first cut

Conventions, matching the repo: allocating constructors take an explicit allocator (MEL-CODE-003);
optional parameters ride an `_opt` struct with a brace-init macro, so there are no silent positional
defaults (MEL-CODE-007); retractable transients (sources, in-flight ops, subscriptions) are
generation-checked **handles** whose stale form resolves to nothing (MEL-ENGINE-VIII), while owned,
lifetime-stable objects (executors, demux, loop, scope) are pointers; no `kind` enum gates behaviour —
open objects sit behind a vtable, the kinds are distinct constructors (MEL-CODE-001).

### Executors

Each kind is constructed once and handed out as a `Mel_Executor*`:

```c
Mel_Executor* mel_executor_inline(void);

Mel_Serial*   mel_serial_create_opt(Mel_Serial_Opt opt);
Mel_Executor* mel_serial_executor(Mel_Serial* s);

Mel_Pool*     mel_pool_create_opt(Mel_Pool_Opt opt);
Mel_Executor* mel_pool_executor(Mel_Pool* p);

Mel_Blocking* mel_blocking_create_opt(Mel_Blocking_Opt opt);
Mel_Executor* mel_blocking_executor(Mel_Blocking* b);

Mel_Executor* mel_realtime_executor(Mel_Inbound* audio);
```

The realtime handle asserts on alloc/lock/syscall (MEL-ENGINE-VIII). Per-core is an array of loop
bundles, one pinned per core, each handing out its own executor.

### Demux and the loop bundle

```c
Mel_Demux* mel_demux_create_opt(Mel_Demux_Opt opt);
i64        mel_demux_drive(Mel_Demux* d, i64 timeout_ns);
void       mel_demux_host(Mel_Demux* d, Mel_Host_Loop host);

Mel_Loop*     mel_loop_create_opt(Mel_Loop_Opt opt);
Mel_Executor* mel_loop_executor(Mel_Loop* l);
Mel_Demux*    mel_loop_demux(Mel_Loop* l);
int           mel_loop_run(Mel_Loop* l);
void          mel_loop_attach(Mel_Loop* l, Mel_Host_Loop host);
void          mel_loop_quit(Mel_Loop* l);
```

`Mel_Loop` is the bundle — serial executor + demux + timer — run driven (`run`, blocks until `quit`) or
hosted (`attach`). It stays decomposable: assemble the three by hand when a use needs to (MEL-ENGINE-II).

### Sources

The open seam, then convenience constructors over it; each binds a demux and routes to a target:

```c
Mel_Source*       mel_source_new(const Mel_Source_Vtable* vt, usize size, const Mel_Alloc* alloc);
Mel_Source_Handle mel_source_attach(Mel_Demux* d, Mel_Source* s, Mel_Waker on_ready);
void              mel_source_retract(Mel_Source_Handle h);

Mel_Source_Handle mel_source_fd    (Mel_Demux* d, i64 fd, u32 interest,       Mel_Executor* target, Mel_Task* on_ready);
Mel_Source_Handle mel_source_timer (Mel_Demux* d, i64 deadline_ns, i64 period, Mel_Executor* target, Mel_Task* on_ready);
Mel_Source_Handle mel_source_signal(Mel_Demux* d, i32 signo,                   Mel_Executor* target, Mel_Task* on_ready);
Mel_Source_Handle mel_source_poll  (Mel_Demux* d, bool (*pred)(void*), void* user, i64 every_ns, Mel_Executor* target, Mel_Task* on_ready);
```

The `(target, on_ready)` pair *is* the routing — change the executor, handle the same readiness in any
context. The convenience constructors embed a resubmit cell so they stay zero-alloc; the open
`mel_source_new` + a raw `Mel_Waker` covers a non-executor edge (set an SPSC flag the audio thread
polls). Retract is generation-safe: after it returns, the source never fires again.

### Inbound adapters

```c
Mel_Inbound* mel_inbound_inline (Mel_Executor* here);
Mel_Inbound* mel_inbound_handoff(Mel_Executor* target);
void         mel_inbound_deliver(Mel_Inbound* in, Mel_Task* task);
```

The OS-push callback funnels into `mel_inbound_deliver`. `inline` runs the task in place (audio over the
realtime executor; UI on main); `handoff` posts to a target (a window message processed on the pool).
The inline-or-handoff choice is the constructor, visible at the call site.

### I/O

```c
typedef struct {
    i64 fd; void* buffer; usize len; i64 offset;
    Mel_Executor* deliver;
    Mel_Io_Op* out_op;
} Mel_Io_Read_Opt;

Mel_Io*     mel_io_create_opt(Mel_Io_Opt opt);
Mel_Future* mel_io_read_opt(Mel_Io* io, Mel_Io_Read_Opt opt);
#define     mel_io_read(io, ...) mel_io_read_opt((io), (Mel_Io_Read_Opt){ __VA_ARGS__ })
```

`deliver` is the executor the future's continuation lands on, required, no default; `out_op` is the
cancel handle. Over a native-completion demux the op goes straight to the submission queue; over a
readiness demux the completion shim runs the non-blocking syscall.

### Coordination wiring

```c
mel_future_then(f, &cont.task, target);
mel_event_subscribe_push(ev, target, cb, user);
mel_channel_recv_future(ch, out, &fut, target, alloc);
```

Every coordination object names the executor its continuation lands on — the same `target` parameter a
source carries. The blocking-flavour counterparts (`mel_channel_recv`, `mel_counter_wait`) park a worker
fiber instead.

### Scope

```c
typedef struct { void* obj; void (*cancel)(void* obj); } Mel_Cancellable;
Mel_Scope* mel_scope_create(Mel_Scope* parent, const Mel_Alloc* alloc);
void       mel_scope_adopt(Mel_Scope* sc, Mel_Cancellable c);
void       mel_scope_cancel(Mel_Scope* sc);
Mel_Scope* mel_scope_deadline(Mel_Scope* parent, Mel_Demux* d, i64 ns, const Mel_Alloc* alloc);
```

`Mel_Cancellable` is a cancel-closure, so a scope adopts heterogeneous children (future, source handle,
io op) without an enum. Cancel walks children and cancels before free; `deadline` is a scope bounded by
a timer source.

### A wired example

Detect on one demux, handle in two contexts — a frame timer on the loop, a socket read on the pool,
bounded by a five-second deadline:

```c
Mel_Loop* loop = mel_loop_create(.alloc = a);
Mel_Pool* pool = mel_pool_create(.alloc = a, .workers = 8);
Mel_Io*   io   = mel_io_create(.alloc = a, .demux = mel_loop_demux(loop));

mel_source_timer(mel_loop_demux(loop), mel_now() + mel_ms(16), mel_ms(16),
                 mel_loop_executor(loop), &frame.task);

Mel_Scope*  sc = mel_scope_deadline(nullptr, mel_loop_demux(loop), mel_sec(5), a);
Mel_Future* f  = mel_io_read(io, .fd = sock, .buffer = buf, .len = n,
                             .deliver = mel_pool_executor(pool), .out_op = &op);
mel_scope_adopt(sc, (Mel_Cancellable){ f, (void (*)(void*))mel_future_cancel });
mel_future_then(f, &on_read.task, mel_pool_executor(pool));

mel_loop_run(loop);
```

Under CFRunLoop / ALooper / the browser, swap `mel_loop_run` for `mel_loop_attach(loop, host)` —
nothing else changes.

### Interface forks to settle

- **Backend selection** — a per-platform compile-time default plus explicit constructors
  (`mel_demux_create_io_uring`, `_epoll`) to force one, *or* a runtime string id validated per platform
  (as the build selects the GPU backend). Lean: compile default + explicit constructors; runtime id only
  if one binary must switch at startup.
- **Routing default** — require `target`/`deliver` at every source and op, *or* default to the demux's
  loop executor when omitted. Lean: require it (MEL-CODE-007); silence here is the debugging tax the
  guideline warns against.
- **Loop as type vs sugar** — keep a `Mel_Loop` convenience type, *or* expose only the three parts and
  let the user bundle them. Lean: both — a thin type that remains fully decomposable (MEL-ENGINE-II).
- **One demux per loop vs shared** — a loop owns one demux, *or* a demux is shared across executors.
  Lean: one-per-loop as the norm; the source's `target` already decouples handling, so sharing is
  rarely needed.

## Platform realization

Each axis selects a backend per platform; nothing else changes.

- **Linux** — Demux: io_uring (completion) or epoll (readiness); timers timerfd; self-wake eventfd;
  signals signalfd; fs-watch inotify; pool futex-parked.
- **macOS / iOS** — Demux: kqueue (readiness+timer+signal+vnode+user) or GCD; the main thread is a
  Hosted demux over CFRunLoop; audio is a realtime inbound adapter (CoreAudio); vsync CVDisplayLink.
- **Windows** — Demux: IOCP (completion) + RIO for sockets; the UI thread is a Hosted demux over the
  message pump; self-wake `PostQueuedCompletionStatus`/`PostThreadMessage`; waitable timers; pool via
  the OS thread pool or our own.
- **Android** — Demux: a Hosted demux over ALooper (epoll); vsync Choreographer; audio AAudio realtime
  adapter; sensors via callback inbound adapters.
- **Web / WASM** — only a Hosted demux over the browser event loop; the main thread never blocks; the
  Pool executor is Web-Worker-backed with `postMessage` handoff; suspension via JSPI or continuation;
  coordination objects work without blocking.
- **WASI** — single-threaded driven `poll_oneoff` demux; self-wake is a no-op until threads exist.

## Pattern walkthroughs (coverage)

- **Main-thread poll** — a Poll Source in the loop's Demux, target = loop executor.
- **Poll on another thread, handle on main** — the *same* Poll Source in a dedicated-thread Demux,
  target = main executor. One concept, one parameter changed.
- **OS message handled off the UI thread** — an Inbound adapter on the message-pump thread, handoff to
  the Pool executor; the UI thread returns to pumping immediately.
- **Socket server** — io_uring Demux (driven), Completion Sources for accept/read/write resolving
  futures; per-core shard for scale; or one Pool for simplicity.
- **GPU completion** — a fence Completion/Poll Source → future on the render-thread executor.
- **Realtime audio** — Inbound adapter on the CoreAudio thread + realtime-constrained executor +
  continuation resume + SPSC rings to and from non-RT; zero alloc/lock by construction.
- **Timeout** — timer Source raced against the op's Completion Source via `when_any`; the loser is
  cancelled through the same one-shot CAS.

## Failure modes (stress test)

- **Source fires while being retracted** — retract wins or loses a single CAS against fire; after
  retract returns the Waker is dead (generation-checked); no fire to a freed handler.
- **Target executor torn down under a live Source** — the Source's lifetime is Scope-bound to its
  target; the Scope cancels and retracts Sources before the executor frees.
- **Hosted-loop reentrancy** — when the OS calls our Source/adapter and the handler submits back to the
  *same* loop executor, the submit defers to the next turn (trampoline/next-turn rule); the OS loop is
  never recursed mid-callback.
- **Driven demux sleep-vs-arrival race** — a self-wake (User) Source plus arm-then-recheck: a `submit`
  from another thread enqueues, then fires the User source; the demux re-checks pending after deciding
  to sleep, so a wake racing the sleep is never lost.
- **Cross-core waker ordering** — waking core B from core A uses B's self-wake; order is per-Source FIFO
  only; no global cross-core order is promised.
- **Completion cancel divergence** — io_uring async-cancel is itself async, IOCP `CancelIoEx` may race
  completion; `cancel` returns *requested* and the outcome resolves through the one-shot CAS (completed
  XOR cancelled, never both).
- **Readiness→completion shim partial I/O** — edge-triggered demux + short read: the shim loops the
  non-blocking syscall until EAGAIN before synthesizing completion; level vs edge is a backend detail,
  not a surface one.
- **Realtime adapter discipline** — the constrained executor asserts on any alloc/lock/syscall; the
  only legal transport across the RT boundary is preallocated SPSC; suspension is continuation only.
- **Web no-block** — no Driven demux on the main thread; fiber-block is illegal there; the model
  degrades to Hosted demux + Worker-backed pool + JSPI/continuation, and the same code shape holds.
- **Backpressure** — the wakeup path is unbounded and never-fail; every bounded queue (channel ring,
  casual call, cross-core MPSC) carries an explicit, logged policy (MEL-ENGINE-VIII).
- **Thread-per-core connection migration** — a connection pinned to core A that must rebalance is an
  explicit handoff Task to core B's executor, never shared mutable access.
- **Lost-wakeup on coordination** — resolve and register race is closed by register-then-recheck with
  the publish and recheck of both coordination words sequentially consistent.

## Open decisions

- Whether Scope is its own module or folds into the coordination objects.
- The exact published memory-order contract on `Waker.wake` / `Executor.submit` (release-enqueue /
  acquire-dequeue assumed throughout).
- How far IOCP and the browser loop can be integrated as Driven before a Hosted fallback is forced.
- Whether the timer-wheel is a Demux backend or a Source layered on any Demux's timeout.
