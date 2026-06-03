# executor

The thin waist of Melody's async substrate: `Mel_Task`, `Mel_Executor`, `Mel_Waker`, and the
inline executor. Plain callables, fibers, and continuations all run over this joint; the executor is
blind to which suspension technique (if any) a task body uses.

Deps: core, allocator. (The test additionally pulls collection for `mel_container_of`.)

## Waist types

```c
struct Mel_Task {
    void (*run)(Mel_Task* self);
    Mel_Task* _Atomic next;
    _Atomic(i32) armed;
};
struct Mel_Executor { void (*submit)(Mel_Executor* self, Mel_Task* task); };
typedef struct { void (*wake)(void* user); void* user; } Mel_Waker;
```

`Mel_Task` is **intrusive**: the owner embeds it (fiber parking node, continuation frame, future
record) and recovers its struct by `mel_container_of`. On the wakeup path this makes `submit`
unbounded, zero-alloc, never-fail (Vyukov intrusive MPSC, when a queued backend is used).

`armed` is the coalescing gate. `submit` performs `armed` CAS `0 -> 1` and enqueues only on success;
an already-armed (still-queued) task is a no-op. The owner of the queued task clears `armed -> 0`
immediately before invoking `run`, so a re-submit from within `run` re-arms and re-queues.

Affinity (realtime / can-block / ordered / main-thread) is **not** waist data — it is which executor
handle you hold, enforced at the violation site, never a flag (MEL-CODE-001). The capability bitset
the earlier design carried was deliberately removed; it is not reintroduced here in any form.

## Inline executor

`mel_executor_inline()` returns a process-wide `Mel_Executor*` whose `submit` runs the task on the
calling thread, **trampolined** through a thread-local drain list:

- The outermost `submit` on a thread becomes the drainer: it processes the submitted task plus any
  task enqueued while it runs, FIFO, until the list empties.
- A `submit` issued while a drain is in progress on the same thread only enqueues, then returns. It
  is **never recursive** and **never re-entrant mid-task**: the stack does not grow with submission
  depth, and a task always observes the previous task as fully returned.

This is the only executor in scope. The inline submit path is allocation-free and branch-minimal.

Memory order is calibrated to the thread-local, single-thread drain: `armed` is CAS'd `acq_rel`
(success) / `acquire` (failure) on `submit` and cleared `release` before `run`; `next` and the
head/tail links are written and read `relaxed`, which is correct only because the list never crosses
a thread on this backend. A queued (MPSC-backed) executor that publishes `next` across threads **must
strengthen** the `next` store to `release` on enqueue and the dequeue load to `acquire` (the
release-enqueue / acquire-dequeue contract the design assumes); the relaxed `next` here is an
inline-only specialization, not the waist's cross-thread contract.

## Re-submit waker

A caller-owned cell `{Mel_Executor* exec; Mel_Task* task;}` exposes a `Mel_Waker` via
`mel_resubmit_waker(cell)`. Its `wake` re-submits the task through the cell's executor, honoring
`armed` coalescing via `submit`. Zero allocation, no waist data.

## Casual fire-and-forget

`mel_executor_call(exec, fn, data, alloc)` allocates a small node from `alloc`, wraps `fn(data)` in a
task, and submits it. The node frees itself after `fn` returns.

Backpressure / ownership note: this is the **casual** path, not the hot/suspended path. It allocates
(one node per call, from the caller's allocator — allocator discipline, no hidden `mel_malloc`). The
allocator is a required parameter; there is no silent default (MEL-CODE-007). `data` is borrowed: the
caller must keep it alive until `fn` runs (for the inline executor `fn` runs before `call` returns,
unless issued mid-drain, in which case it runs before the outermost `submit` returns). A bounded-queue
policy for queued backends (drop / block / fail, explicit and logged) belongs to those backends'
`submit`, not to this waist; the inline executor's drain is unbounded and never-fail.

## Future work (out of scope here — MEL-ENGINE-VIII confession)

The substrate design names two further executors that are **not** implemented in this module, because
they depend on modules not yet present on this path:

- **reactor executor** — `mel_reactor_executor(r)` (submit = always-next-turn defer) plus
  `mel_reactor_defer`. Additive to the existing reactor; depends on the reactor and collection.mpsc.
- **worker-pool executor** (job) — fiber-backed pool so a task may park on a counter; depends on
  collection, thread, fiber, signal.

These are later waves. They are described here as direction, deliberately **not** stubbed in code, so
no dead capability sits in the tree.
