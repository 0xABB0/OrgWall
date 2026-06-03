# Async Substrate — Design

One coherent asynchrony substrate spanning **readiness** (reactor) and **completion** (proactor),
over a thin executor/waker waist on which plain functions, fibers, and continuations all run.
Composability, speed, and each use case (game, GUI, TUI, server, realtime audio, async fs/net/AV)
are first-class. A consumer buys only the modules it needs.

## Spine

Eager **executor + waker** (Asio/tokio shape). `std::execution` (senders/receivers) is the
conceptual what/where/when guide, **not** the API — laziness is zero-cost only under C++ templates;
in C it would force allocation/indirection per combinator. The readiness joint is the Rust `Waker`.
The sole execution atom is a **plain callable**; `fiber` and `continuation` are suspension
*techniques* that produce tasks — the executor is blind to which (if any) a task body uses.

## Waist

```c
typedef struct Mel_Task Mel_Task;
struct Mel_Task {
    void (*run)(Mel_Task* self);
    Mel_Task* _Atomic next;
    _Atomic(i32) armed;
};

typedef struct Mel_Executor Mel_Executor;
struct Mel_Executor {
    void (*submit)(Mel_Executor* self, Mel_Task* task);
};

typedef struct {
    void (*wake)(void* user);
    void* user;
} Mel_Waker;
```

- The task is **intrusive**: the owner embeds it (fiber parking node, continuation frame, future
  record) and recovers its struct by `container_of` (the reactor-source idiom). This makes `submit`
  on the wakeup path **unbounded, zero-alloc, never-fail** (Vyukov intrusive MPSC).
- `armed` coalesces wakes: `wake` does `armed 0→1`; already-armed (still queued) is a no-op.
- `submit` is callable from any thread; its backend does whatever wake it needs (reactor MPSC+wake,
  pool push+wake-a-worker, inline trampoline).
- A re-submit waker is a caller-owned `{Mel_Executor*; Mel_Task*}` cell exposing `Mel_Waker` — no
  waist data, no allocation.
- Capability (realtime / can-block / ordered / main-thread) is **not** waist data — it is which
  executor handle you hold, enforced at the violation site, never a flag (MEL-CODE-001).
- Casual fire-and-forget: `mel_executor_call(exec, fn, data, alloc)` allocates a node and may apply
  an explicit bounded-queue policy. The hot/suspended paths never allocate.

## Modules (role · reference · deps)

- **collection.{mpmc, wsq, mpsc}** — lock-free queues; one vetted set (mpmc + work-stealing exist;
  intrusive MPSC is what the wakeup path needs). *Vyukov, Chase–Lev.* deps: core, allocator.
- **fiber** — stackful switch, pure mechanism. *boost.context, ND GDC'15.* deps: core, allocator.
- **continuation** — stackless, POD, resume-by-index; realtime-legal, snapshot/reload-safe. The
  primitive no other runtime has. *Reynolds defunctionalization.* deps: core.
- **executor** — the waist: `Mel_Task`, `Mel_Executor`, `Mel_Waker`, the inline executor. *Asio
  executor + Rust Waker.* deps: core, allocator, collection.
- **reactor** *(additive only)* — main-thread executor + readiness demux (fd/timer/idle/native).
  Gains `mel_reactor_executor(r)` (submit = always-next-turn defer) and `mel_reactor_defer`,
  distinct from `post` (inline-if-owner). Shape unchanged. *GMainContext/GSource, libuv.* deps:
  core, allocator, collection, executor.
- **job** — worker-pool executor; fiber-backed so a task may park on a counter. *ND/enkiTS/Marl +
  tokio.* deps: core, allocator, collection, thread, fiber, signal, executor.
- **signal** — fiber-blocking dual of a future (`Mel_Counter`); bridges to `future_wait`. *ND
  counters.* deps: core, fiber.
- **future** — one-shot result; CAS `pending → (resolved|cancelled)`; single continuation
  (`then` to a target executor); cross-thread handoff via queue acquire/release; `when_all`/
  `when_any`; cancellation via scope. Fan-out is **event**, not future. *Folly Future; in-repo
  `Mel_Gpu_Future`.* deps: core, allocator, executor.
- **event** — many-shot pub/sub; generation-checked subscriptions; per-channel loss policy
  (latest / lossy-lag / lossless), explicit. Subsumes clipboard-watch, display events, input.
  *GCD dispatch sources, tokio watch/broadcast.* deps: core, allocator, collection, executor.
- **channel** — CSP point-to-point stream, M producers → N consumers. Unbuffered (rendezvous: a
  direct sender→receiver handoff, zero copy when both present) or buffered (a bounded ring sized at
  creation). `send`/`recv` park a fiber (job world) or return a future (callback world) — the same
  dual as `signal`/`future`. `select` waits on multiple channel ops and proceeds with the first
  ready. `close` drains then reports closed; send-after-close is an error (MEL-ENGINE-VIII). Waiter
  queues are intrusive (parked senders/receivers are owner-embedded nodes, no alloc). *Hoare CSP,
  libmill/libdill `chan`/`choose`, Go channels.* deps: core, allocator, collection, executor,
  signal, future. The coordination trio splits by topology: **future** 1→1 one-shot, **event** 1→N
  broadcast, **channel** M→N stream with backpressure + rendezvous + select.
- **port** *(platform-shaped)* — proactor; submit OS async ops, completions resolve futures on a
  target executor. **Two modes:** reactor-source CQ polling where the OS allows (io_uring, kqueue,
  GCD) — zero thread-hop; own-thread drain + cross-submit fallback (classic IOCP). *libuv internals,
  io_uring/IOCP, Seastar IO engine.* deps: core, allocator, collection, executor.
- **scope** — structured cancellation/lifetime; a parent cancels its children; teardown cancels
  pending futures **before** their executor frees. *libdill, Kotlin structured concurrency.* deps:
  core, executor.

DAG (acyclic): `collection → executor → {reactor, job, future, event, port, scope}`;
`fiber → {signal, coroutine, job}`; `signal → job`; `{executor, signal, future} → channel`. `port`
delivers to an `Mel_Executor*` handle, so it composes with reactor *or* job without depending on
either.

## Invariants

1. Affinity is an executor handle, never an enum; new executor kinds never touch the protocol.
2. The realtime hot path bypasses the waist: it resumes a continuation by plain call. The
   abstraction is opt-in coordination, absent on the hot path (MEL-ENGINE-III).
3. Inline-vs-deferred is the executor's property; the reactor executor always defers; the inline
   executor trampolines via a thread-local drain (never recursive, never re-entrant mid-task).
4. Proactor is the portable surface; readiness platforms are wrapped up to it.
5. Per-core share-nothing is permitted, not forced; the fast path carries no mandatory cross-core
   sync.
6. Cancellation, lifetime, and backpressure are first-class and loud — bounded queues carry an
   explicit policy, logged; the wakeup path is unbounded and never-fail.

## Failure modes (stress test)

- **Waker/cancel race** → one-shot CAS `pending→(resolved|cancelled)`, single wake, register-then-
  recheck; loser frees its value.
- **Backpressure on the wakeup path** → intrusive unbounded MPSC, owner-embedded node; never
  alloc/block/fail. Casual path bounded + explicit policy.
- **Double-queue on many-shot wake** → `armed` coalescing.
- **Inline recursion / re-entrancy** → thread-local trampoline.
- **Submit vs executor teardown** → structured scope cancels pending futures before the executor
  frees; cancellation precedes free.
- **Proactor thread-hop** → reactor-source CQ polling on the loop where the OS allows; thread-drain
  only as fallback.
- **Slow / unsubscribing event subscriber** → generation-checked handle; per-channel loss policy.
- **Snapshot of an async-awaiting continuation** → snapshotable only with no waker outstanding;
  async-wait is an honest non-snapshotable boundary (the external I/O isn't snapshotable either).
- **Ordering** → per-producer FIFO + serial-executor linearization only; no global cross-thread
  order promised.
- **Realtime guarantee without caps** → by construction + audio-thread assertion, not a runtime flag.
- **Channel select fairness / close race** → `select` registers an intrusive waiter on every
  candidate, commits the first to fire via a single CAS, and retracts the rest; a `close` racing a
  parked `send`/`recv` is resolved by the same one-shot CAS (the waiter wakes either committed or
  closed, never both); rendezvous hands the value sender→receiver directly with no buffer copy.

## Rewrites this forces (consumers, not the reactor)

- **gpu** — drop `Mel_Gpu_Future`/`Mel_Gpu_Completion_Pump`; use `future` + a fence-poller `port`.
- **clipboard** — drop the per-job 0-timer + slotmap-job lifecycle; ops return a `future`, watch is
  an `event`.
- **display** — pull `poll_events` becomes an `event` channel (push and pull both supported).

## Open questions

- How far IOCP can be loop-integrated (GQCS with zero timeout on the loop thread) before the
  thread-drain fallback is needed.
- Whether `scope` is its own module or folds into `future`.
- The exact memory-order contract published on `submit` / `wake` (release-enqueue / acquire-dequeue
  assumed throughout).
