# Runtime Tier — Definition and Contents

## What "runtime" means in Melody

The **runtime tier** is the substrate between the OS/libc and application logic. The things every Melody application needs to *run* — schedule work, switch contexts, synchronize, manage time, come up cleanly, go down cleanly — that are not domain code.

Not application. Not OS. The layer that turns "a program linked against libc" into "a program with a coherent execution model".

This document records what belongs in `modules/runtime/` and why, plus the reorg from the current `modules/async/*` layout.

## Categories

Runtime members fall into eight categories:

### Control-flow primitives
The "how does execution suspend and resume" layer.
- Fibers / stackful contexts (Melody's `runtime/fiber`)
- Continuations (first-class or simulated)
- Stack management: stack pools, guard pages
- `setjmp`/`longjmp`-style nonlocal jumps
- Stack unwinding (the machinery panics ride on)

### Scheduling
The "what runs next" layer.
- Task / job systems (Melody's `runtime/scheduler`, was `async/job`)
- Coroutine / fiber schedulers
- Thread pools, work-stealing pools
- Run loops, frame loops, tick drivers
- Priority queues, deadline scheduling

### Threading
The OS-thread side.
- Thread creation/join wrappers
- Thread-local storage (TLS), fiber-local storage (FLS)
- CPU affinity, thread naming, priority

### Synchronization
The cross-context coordination primitives.
- Mutex, rwlock, condvar, semaphore, once-init, barrier, latch
- Atomics wrappers (typed, memory-order-explicit)
- Channels / mailboxes (typed SPSC/MPSC/MPMC)
- Wait groups, parking lots, futexes

### Time
The runtime's clocks.
- Monotonic / wall / high-res clocks (Melody's `time/clock`, `time/nano`)
- Timer wheels, timeout queues (`runtime/timer`)
- Deadlines, frame pacing, sleep abstractions

### Lifecycle
Bringing the runtime up and down.
- Module init/shutdown ordering (static-init-order-fiasco solution)
- Panic / abort / crash handlers, backtrace capture
- Signal handlers
- At-exit hooks

### Dynamic code
Runtime composition.
- Shared-library / DLL loading, symbol resolution
- Plugin systems, hot reload, code patching
- JIT support (if needed)

### Introspection / debugging substrate
Observation of the runtime itself.
- Backtrace / symbolization (Melody's `debug/stacktrace`)
- Profiler hooks, sampling profilers, scope timers
- Tracing spans (the primitive; the exporter is in `telemetry/`)
- Memory accounting / leak tracking integration (Melody's `allocator.leak`)

## Borderline cases

Things commonly debated:

- **Allocators.** Often top-level — they are substrate *for* the runtime itself, not part of it. Melody's `allocator/` stays separate. Runtime modules accept `Mel_Alloc*`.
- **Async I/O.** A *different* category: it is about waiting on the kernel/external events. It builds on runtime (uses fibers + scheduler + timers) but is not itself runtime. See `async-first.md`.
- **Logging.** Application concern, not runtime — though *tracing* (structured spans tied to the scheduler/task system) arguably is.
- **IPC / channels.** Channels-as-primitive are runtime; high-level message-passing frameworks are domain.

## Melody — what goes in `modules/runtime/`

Reorg from current layout:

```
modules/async/fiber/          → modules/runtime/fiber/
modules/async/job/            → modules/runtime/scheduler/
modules/async/coroutine/      → modules/runtime/coroutine/
modules/async/signal/         → modules/os/signal/  OR  modules/event/bus/
                                 (depending on what it actually is —
                                  POSIX signal vs pub/sub signal)
```

New runtime modules to create (in build order from `async-first.md`):

- `runtime/thread` — OS thread wrappers, TLS, FLS, affinity, naming
- `runtime/sync` — mutex/rwlock/condvar/semaphore/atomics/once/barrier
- `runtime/channel` — typed bounded/unbounded SPSC/MPSC/MPMC; sync and async-aware
- `runtime/cancel` — cancellation tokens (cooperative, composable)
- `runtime/buffer` — owned buffers with explicit in-flight state for async I/O
- `runtime/timer` — timer wheel
- `runtime/reactor` — I/O event loop wrapping io_uring/IOCP/kqueue
- `runtime/task` — unified awaitable handle over fibers + worker-pool jobs
- `runtime/lifecycle` — init/shutdown ordering, atexit, module dependency graph
- `runtime/panic` — abort path: stacktrace dump, diagnostics, handlers
- `runtime/plugin` — dlopen/LoadLibrary wrappers, hot reload (deferred)

Existing runtime-adjacent modules to leave where they are:

- `modules/allocator/` — substrate *for* runtime; not part of it
- `modules/debug/` — observation, lives next to runtime
- `modules/time/` — clocks belong here, timers move into runtime
- `modules/process/` — really an `os/process`, see `modules-overview.md`

## Distinguishing `runtime/coroutine` from async

Melody's coroutine layer (`mel_coro_create`, `mel_coro_yield`, `mel_coro_wait`, `mel_coro_update(dt)`) is *cooperative tasks driven by a tick*. It is policy (the scheduling model) on top of substrate (`runtime/fiber`).

That policy is fine — game loops want exactly this. But it is **not** the same thing as async-I/O-driven cooperative tasks, which yield until a kernel completion fires.

Both should exist. They share `runtime/fiber` as substrate. The tick-driven scheduler is one driver; the reactor-driven scheduler is another. `runtime/scheduler` should accommodate both, or there should be two named drivers and one unified `runtime/task` handle.

See also: `fiber-and-coroutine.md`, `async-first.md`, `modules-overview.md`.
