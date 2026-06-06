# Async Substrate

One coherent asynchrony substrate spanning **readiness** (reactor) and **completion** (port), over a
thin executor/waker waist on which plain functions, fibers, and continuations all run. Game, GUI, TUI,
server, realtime audio, and async fs/net/AV are each first-class. A consumer links only the modules it
needs; the parts compose rather than nest (MEL-ENGINE-IX).

The shape is the eager **executor + waker** of Asio/tokio, not the lazy sender/receiver of
`std::execution`: laziness is zero-cost only under C++ templates; in C it would force an
allocation/indirection per combinator. The sole execution atom is a **plain callable**. `fiber` and
`continuation` are suspension *techniques* that produce such callables — the executor is blind to which
(if any) a task body uses.

## The model in four parts

The substrate is four orthogonal axes the user joins as he wills:

1. **The waist** — the universal task/executor/waker joint everything submits through (`executor`).
2. **Suspension** — *how* a task body parks and resumes: not at all, stackful (`fiber`), or stackless
   POD (`continuation`).
3. **Coordination** — *how* a value travels between tasks, split by topology: 1→1 (`future`), 1→N
   (`event`), M→N (`channel`); with `signal` the fiber-blocking dual of `future`.
4. **I/O** — the two ways the OS feeds the loop: readiness demux (`reactor` sources) and completion
   (`port` proactor).

Affinity — realtime / can-block / ordered / main-thread — is **never** a flag on any of these. It is
*which executor handle you hold*, enforced at the violation site (MEL-CODE-001).

## 1. The waist — `executor`

The whole substrate narrows to three types:

```c
struct Mel_Task {
    void (*run)(Mel_Task* self);
    Mel_Mpsc_Node link;
    _Atomic(i32) armed;
};

struct Mel_Executor {
    void (*submit)(Mel_Executor* self, Mel_Task* task);
};

typedef struct { void (*wake)(void* user); void* user; } Mel_Waker;
```

`Mel_Task` is **intrusive**: the owner embeds it (fiber parking node, continuation frame, future
record, event delivery node) and recovers the enclosing struct with `mel_container_of`. The embedded
`Mel_Mpsc_Node link` is a `collection.mpsc` node, so on the wakeup path `submit` is an **unbounded,
zero-alloc, never-fail** enqueue (Vyukov intrusive MPSC) — there is no queue to overflow and no
allocation to fail (MEL-ENGINE-VIII).

`armed` coalesces wakes. `submit` does CAS `armed 0→1` (`acq_rel`/`acquire`) and enqueues only on
success; a task already queued is a no-op. The consumer clears `armed→0` (`release`) immediately
**before** invoking `run`, so a re-submit from inside `run` re-arms and re-queues without growing the
stack.

A re-submit waker is a caller-owned cell, no waist data and no allocation:

```c
typedef struct { Mel_Executor* exec; Mel_Task* task; } Mel_Resubmit_Cell;
Mel_Waker mel_resubmit_waker(Mel_Resubmit_Cell* cell);
```

`mel_executor_call(exec, fn, data, alloc)` is the casual fire-and-forget path: it allocates one node,
wraps a plain `fn`, submits, and self-frees after `run`. The hot and suspended paths never allocate;
this one does, explicitly, against a caller-supplied allocator (MEL-CODE-003).

**Inline executor.** `mel_executor_inline()` is a process-wide executor whose `submit` runs the task on
the calling thread through a **thread-local trampoline drain**: the outermost `submit` becomes the
drainer and processes its task plus anything enqueued while it runs, FIFO, until empty; a `submit`
issued mid-drain only enqueues and returns. It is therefore **never recursive** and **never re-entrant
mid-task** — the stack does not grow with submission depth, and a task always observes its predecessor
as fully returned. Its `link`/`next` writes are `relaxed`; that is an inline-only specialization legal
because the list never crosses a thread, *not* the waist's cross-thread contract (see Memory order).

## 2. Suspension techniques

A task body suspends — or doesn't — by one of three techniques. The executor neither knows nor cares
which (MEL-ENGINE-IX).

**Plain callable.** No suspension. The body runs to completion in one `run`. Combinators and most
glue are this.

**Fiber — stackful.** `modules/fiber` is a pure context-switch mechanism (Boost.Context shape), no
scheduler of its own:

```c
typedef void* Mel_Fiber;
typedef struct { Mel_Fiber from; void* user; } Mel_Fiber_Transfer;
Mel_Fiber          mel_fiber_create(Mel_Fiber_Stack stack, Mel_Fiber_Cb cb);
Mel_Fiber_Transfer mel_fiber_switch(Mel_Fiber to, void* user);
```

A fiber owns a real machine stack (reserved via vmem, first page `PROT_NONE` as an overflow guard) and
switches by saving/restoring callee-saved registers and the stack pointer in hand-written asm
(`x86_64` sysv/macho/ms, `arm64` aapcs elf/macho). A fiber can suspend *anywhere*, through any depth of
helper calls — that is its power and its cost: a machine stack embeds return addresses and self
pointers, so it cannot be snapshotted, relocated, or run where stacks are forbidden. Parking/wakeup is
not fiber's concern; the `job` runtime layers it on via `signal`.

**Continuation — stackless POD.** `modules/continuation` is a stackless coroutine whose suspended
state is a *reified delimited continuation* — a flat,
caller-allocated, fixed-size struct holding an **integer resume index** (never a code pointer) plus
every local live across a suspension.

```c
typedef bool Mel_Cont_Suspended;
#define MEL_CONT_STATE_START 0
#define MEL_CONT_STATE_DONE  (-1)
```

Three capabilities are corollaries of the single property *the suspended state is plain owned data*
(MEL-ENGINE-IX), not separate features:

- **Runs where fibers are forbidden** — `resume` is a plain call against caller memory, legal inside
  the audio realtime callback (no fiber, no reactor, no wait, no alloc).
- **Snapshots** — a flat frame `memcpy`s to disk (save), rewinds (rollback netcode), or replays
  deterministically. A machine stack cannot.
- **Survives hot-reload** — the frame holds no code pointers, so a reloaded module resumes a frame its
  prior incarnation minted, given a stable layout (guarded per-continuation by a layout hash).

A body is authored with markers and minted by a libclang codegen tool (defunctionalization over a
CPS-marked suspension set, emitting a Duff's-device state machine):

```c
mel_cont(ticker, (i32 frames, i32 amplitude), i32) {
    int x = 0;
    mel_cont_yield(x);
    for (i32 t = x; t < frames; t++) mel_cont_yield((amplitude * t) / frames);
    mel_cont_return(amplitude);
}
```

The tool emits `Mel_Cont_Frame_ticker` and `ticker__resume(frame, out)` returning `Mel_Cont_Suspended`
(true while suspended). Suspension is permitted **only** in the marked body, never through a helper
call — the deliberate refusal to become a general coroutine and re-import the function-coloring
problem fibers exist to dodge. `mel_cont_await(child)` composes a child frame to completion without a
scheduler. The module is self-contained and **outside the `nob` build** (it owns its codegen tool and
build driver); its runtime is plain C depending only on `core`.

**Coroutine — the game-loop convenience layer.** `modules/coroutine` is `fiber` plus a tiny
frame/time scheduler: `mel_coro_invoke` / `mel_coro_yield` / `mel_coro_yieldn(n)` / `mel_coro_wait(ms)`
driven by a per-frame `mel_coro_update(ctx, dt)`. It owns its own fiber pool, is single-threaded, and
stands apart from the waist and the `job` runtime — a scripting layer for animation and game logic.

## 3. Executors — where work runs

Three executor backends, each a `Mel_Executor*` handle:

**Inline** — described above; the trampoline. Use when "run it on this thread, now, without growing
the stack" is what you mean.

**Reactor** — `modules/reactor` is the **main-thread executor + readiness demux**, an abstraction over
the platform loop (Win32 message pump, POSIX `poll`, Apple `CFRunLoop`, Android `ALooper`, web rAF),
not a loop of its own. Two distinct deferral paths:

- `mel_reactor_post(r, cb, user)` runs `cb` **inline if called on the owner thread**, else pushes to a
  cross-thread MPSC and wakes the loop.
- `mel_reactor_executor(r)` returns an executor whose `submit` is `mel_reactor_defer` — it **always**
  enqueues for next turn (`armed` coalescing, MPSC, wake), even on the owner thread.

It runs `MEL_REACTOR_THREADED` (owns its thread; `spawn` blocks until `quit`) or
`MEL_REACTOR_ATTACHED` (rides a host loop; `spawn` returns and hands the next turn to the host). All
source mutation is unsynchronized and must happen on the loop thread; cross-thread work routes through
`post`/`defer`. Structural edits during dispatch (`detach`/`destroy`) defer to a reap after the
iteration unwinds.

**Job** — `modules/job` is the **worker-pool executor, fiber-backed**, so a task may park mid-flight
on a counter and the worker steals other work:

```c
void          mel_job_run(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, ...);
void          mel_job_yield(void);
Mel_Executor* mel_job_executor(void);
```

Each worker runs a fiber from a pool; submission uses a Chase–Lev work-stealing deque per worker plus
an MPMC for cross-worker handoff. A job parks by `signal`/`Mel_Counter` (below); the runtime
reschedules the parked fiber on wake. `mel_job_executor()` hands the pool out as an ordinary executor
so other domains can submit CPU work to it.

## 4. Coordination — how a value travels

The trio splits by topology; `signal` is the fiber-blocking dual that lets job code *block* where
callback code *registers*.

**`future` — 1→1 one-shot.** A write-once cell:

```c
bool  mel_future_resolve(Mel_Future* f, void* value, Mel_Future_Status status);
bool  mel_future_cancel (Mel_Future* f);
void  mel_future_then   (Mel_Future* f, Mel_Task* cont, Mel_Executor* target);
```

`state` is `_Atomic(u32)`: `pending → (resolved | cancelled)` by a single CAS; exactly one of
resolve/cancel wins, the loser is a no-op and a losing resolve frees its rejected value (no leak, no
double free). `then` registers a continuation and the executor it must run on. Delivery is the
**register-then-recheck** rendezvous: resolve publishes `state`, `then` publishes `cont`, each then
re-reads the other; the one that runs last sees both and submits exactly once. A cancelled future still
delivers its continuation, which observes the `CANCELLED` bit — a cancelled await resumes, it does not
leak. A future drives one continuation — a second `then` is asserted misuse, and fan-out is `event`.
`when_all`/`when_any` compose many; `Mel_Future_Scope` (`adopt`/`cancel`/`teardown`) is the
structured-cancellation parent that cancels pending children before their executor frees.

**`event` — 1→N broadcast.** `mel_event_fire` (callable from any thread) fans one fixed-size item to N
subscribers:

```c
Mel_Event_Sub mel_event_subscribe_push(Mel_Event* ev, Mel_Executor* exec, Mel_Event_Callback cb, void* user);
Mel_Event_Sub mel_event_subscribe_pull(Mel_Event* ev, void* user);
void          mel_event_fire(Mel_Event* ev, const void* item);
```

Membership is an atomically published, refcounted **immutable snapshot** — `fire` walks a stable set
without holding the membership lock, so a sibling unsubscribing mid-fanout never frees a node out from
under the walk. Per-subscriber transport is an intrusive pooled FIFO under a per-subscriber spinlock
with O(1) critical sections that never span the user callback; push delivery is coalesced to at most
one outstanding delivery task per subscriber. Overflow is an **explicit** per-channel policy —
`latest` (drop oldest, watch semantics), `lossy_lag` (refuse newest), `lossless` (refuse + warn), or
`custom` — never a silent default (MEL-CODE-007). The channel is refcounted, so `destroy` is safe with
deliveries in flight on any executor.

**`channel` — M→N CSP stream.** Point-to-point with backpressure, rendezvous, and select:

```c
Mel_Channel_Status mel_channel_send(Mel_Channel* ch, const void* item);            /* fiber-blocking */
Mel_Channel_Status mel_channel_recv(Mel_Channel* ch, void* out);                   /* fiber-blocking */
void               mel_channel_send_future(Mel_Channel*, const void*, Mel_Future*, Mel_Executor*, const Mel_Alloc*);
Mel_Channel_Op*    mel_channel_sel_wait(Mel_Channel_Sel* sel);
```

Capacity `0` is an unbuffered **rendezvous** — sender and receiver hand off by a direct slot→slot
`memcpy`, no buffer copy; `N>0` is a bounded ring sized once at creation. A per-channel mutex
serializes the ring, the two intrusive waiter queues, and the `closed` flag for an O(1) decision; it is
never held across a park. Every waiter carries a `group_state` resolved by a **single CAS**
`PENDING → (COMMITTED | CLOSED)` — *committed XOR closed, never both, never neither* — so a `close`
racing a parked op resolves through the same CAS as a counterpart would. `select` registers an
intrusive waiter on every candidate under all-candidate locks taken in address order, commits the
first ready via that shared CAS, and retracts the losers. Two flavors share one channel: **blocking**
(`send`/`recv`/`sel_wait`, legal only on a job worker fiber — they park via `signal`, and live in
`channel_fiber.c` so the callback world never links the job runtime) and **future-returning**
(`*_future`, for the reactor/callback world). Send-after-close is `ERROR | CLOSED`, never silent
(MEL-ENGINE-VIII).

**`signal` — the fiber-blocking dual of `future`.** Where a future *registers a callback*, a signal
*parks the calling fiber*:

```c
typedef struct { _Atomic(i32) state; u32 generation; } Mel_Signal;   /* counter:head packed */
typedef struct { Mel_Signal signal; } Mel_Counter;
void mel_counter_increment(Mel_Counter* c);
void mel_counter_decrement(Mel_Counter* c);   /* wakes parked fibers at zero */
void mel_counter_wait(Mel_Counter* c);
```

`state` packs a counter (low 16 bits) and a park-list head index (high 16 bits) into one atomic; a
generation guards ABA. The set/clear/counter arithmetic lives in `signal`, but `mel_signal_wait` —
the actual fiber park — lives in the `job` runtime and is registered into `signal` through a runtime
callback table. This is the duality the substrate is built on: `signal:future :: blocking:callback`,
and `channel`'s two flavors are exactly this duality exposed on a stream. `Mel_Fiber_Mutex` is the
same primitive as a FIFO-fair mutex for worker fibers.

## 5. I/O — readiness and completion

The OS feeds the loop two ways, and the substrate offers both honestly (MEL-ENGINE-VII).

**Readiness — reactor sources.** A `Mel_Reactor_Source` is anything the loop drives — fd, timer, idle,
GPU presentation waitable — implementing `prepare`/`check`/`dispatch`/`finalize` hooks the loop calls
at canonical phases (`mel_reactor_timer_new`, `mel_reactor_idle_new`, or a custom source with polls).
You ask "tell me when this fd is readable," then do the read yourself. This is the GMainContext/libuv
shape.

**Completion — `port` proactor.** You submit the operation; the OS performs it; you get the result:

```c
Mel_Future* mel_port_read (Mel_Port* port, /* fd, buffer, len, offset?, deliver?, out_op? */ ...);
Mel_Future* mel_port_write(Mel_Port* port, ...);
bool        mel_port_cancel(Mel_Port* port, Mel_Port_Op op);
```

A submitted op embeds its `Mel_Future` (zero-alloc wakeup) and resolves on a **target executor** when
it completes. Two backends by what the OS affords: **CQ-poll** as a reactor source where the queue can
be drained on the loop (io_uring/kqueue/GCD) — no thread hop; **own-thread drain + cross-submit**
where it cannot (classic IOCP). Because completion lands on an `Mel_Executor*` handle, `port` composes
with `reactor` *or* `job` without depending on either. The proactor is the portable surface; readiness
platforms are wrapped up to it.

## Choosing a primitive

- Run a callback on the loop, now-if-owner: `mel_reactor_post`. Always next turn: the reactor executor.
- Parallel CPU work that may itself block: `job`.
- One result, one consumer: `future`. Many consumers of the same event: `event`. A stream with
  backpressure, rendezvous, or select: `channel`.
- Block a worker fiber until a count drains: `signal`/`Mel_Counter`. The same wait as a callback:
  `future`. (Channel exposes both as `send`/`recv` vs `*_future`.)
- Suspend with a stack, anywhere, through helpers: `fiber` (via `job`). Suspend stackless, in
  audio/realtime, or to snapshot/hot-reload: `continuation`. Frame-timed game-logic scripting:
  `coroutine`.
- Async file/socket I/O: `port`. Wait on platform readiness (fd/timer/idle): a `reactor` source.

## Invariants

1. Affinity is an executor handle, never an enum; new executor kinds never touch the protocol
   (MEL-CODE-001).
2. The realtime hot path bypasses the waist: it resumes a continuation by plain call. Coordination is
   opt-in, absent on the hot path (MEL-ENGINE-III).
3. Inline-vs-deferred is the executor's property: the reactor executor always defers; the inline
   executor trampolines via a thread-local drain, never recursive, never re-entrant mid-task.
4. The proactor is the portable I/O surface; readiness platforms are wrapped up to it.
5. Per-core share-nothing is permitted, not forced; the fast path carries no mandatory cross-core sync.
6. Cancellation, lifetime, and backpressure are first-class and loud — bounded queues carry an explicit
   logged policy; the wakeup path is unbounded and never-fail (MEL-ENGINE-VIII).

## Memory order

The waist publishes on a **release-enqueue / acquire-dequeue** contract: a queued backend that crosses
threads must store `Mel_Mpsc_Node.next` with `release` on enqueue and load it with `acquire` on
dequeue. The inline executor's `relaxed` links are a single-thread specialization of that contract, not
a relaxation of it.

`future`'s register-then-recheck loads and stores **both** `state` and `cont` `seq_cst`: acquire/
release alone permits StoreLoad reordering and would lose the wakeup when resolve and `then` race.
Payload visibility rides the terminal `state` publish — the producer writes `value`/`status` then
releases `state`, and the delivery winner (possibly the registrar, not the producer) acquires `state`
before submitting. The supported cross-thread read of a future's value is therefore **from its
continuation**, never a bare `mel_future_value` poll on another thread.

`channel` and `signal` use `acq_rel`/`acquire`/`release` only; the group-state CAS publishes the slot
copy done under the channel lock. `event` synchronizes snapshot and node lifetime with `acq_rel`
refcounts and per-subscriber spinlocks.

## Modules

Dependency DAG (acyclic): `collection → executor → {reactor, job, future, event, port, channel}`;
`fiber → {signal, coroutine, job}`; `signal → {job, channel}`; `future → {channel, port}`;
`reactor → port`. `continuation` depends only on `core` and is outside the `nob` build; `coroutine`
depends only on `fiber`/`allocator` and is standalone. `port` delivers to an `Mel_Executor*`, so it
composes with reactor or job without depending on either.

`collection` is the lock-free toolbox under the waist: intrusive `mpsc` (the wakeup path), bounded
`mpmc`, Chase–Lev `wsq` (work stealing), `slotmap` (generation-checked handles), and
`mel_container_of`.

## Failure modes

- **Waker/cancel race** → one-shot CAS `pending→(resolved|cancelled)`, single delivery via seq-cst
  register-then-recheck; the losing resolve frees its value.
- **Backpressure on the wakeup path** → intrusive unbounded MPSC, owner-embedded node; never alloc,
  block, or fail. The casual `mel_executor_call` path is the only allocating one, explicitly.
- **Double-queue on a many-shot wake** → `armed` coalescing.
- **Inline recursion / re-entrancy** → thread-local trampoline; the stack never grows with submission
  depth.
- **Submit vs executor teardown** → `Mel_Future_Scope` cancels pending futures before the executor
  frees; `event`'s refcount keeps a channel alive past in-flight deliveries.
- **Proactor thread-hop** → CQ-poll as a reactor source where the OS allows; own-thread drain only as
  the IOCP fallback.
- **Slow / unsubscribing event subscriber** → generation-checked handle + explicit per-channel loss
  policy; sibling-unsubscribe-during-fanout safe via the immutable snapshot.
- **Snapshot of an async-awaiting continuation** → snapshotable only with no waker outstanding; an
  async wait is an honest non-snapshotable boundary (the external I/O is not snapshotable either).
- **Ordering** → per-producer FIFO and serial-executor linearization only; no global cross-thread order
  is promised.
- **Realtime guarantee** → by construction plus an audio-thread assertion, never a runtime flag.
- **Channel select fairness / close race** → an intrusive waiter on every candidate, one shared CAS
  commits the first to fire and retracts the rest; a close racing a parked op resolves through that
  same CAS (committed XOR closed); rendezvous hands the value over directly with no buffer copy.
