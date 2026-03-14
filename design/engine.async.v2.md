# Async Architecture v2: Signals, Fibers, Kernel IO

**Status:** Proposal. Supersedes `engine.async.md`. Affects `async.job`, `async.io`, `async.fiber`, `vfs.*`, engine init.

**References:** Lumix Engine job system, enkiTS, Naughty Dog GDC 2015, Bitsquid resource pipeline.

## Context

### What exists

Three async layers:
- `async.fiber` — raw context switching (boost.context asm). Sound, stays.
- `async.job` — fiber-based N:M scheduler with priority queues, spinlock-guarded lists, semaphore-based sleep, pool-allocated counters.
- `async.io` — SQE/CQE worker thread pool. VFS registers a handler. Workers call blocking POSIX syscalls. Tickets + polling for completion.

VFS sits on top of `async.io` via handler registration. High-level wrappers (mel_vfs_read_file_alloc) submit SQEs and wait on tickets.

`async.task` — DAG orchestrator that chains VFS tickets and jobs. Used by mugen.roster for async character loading.

`async.coro` — single-threaded game-loop coroutines. Driven by `mel_coro_update(dt)`. Yield N frames, wait N ms.

### What's wrong

1. `async.io` is fake async. Blocking POSIX calls on a thread pool pretending to be async. No kernel async IO (io_uring/kqueue/IOCP).
2. Too many async primitives: fibers, coroutines, jobs, IO, tasks. Five models.
3. Job wait mechanism is busy-yield polling. `mel_job_wait_and_del` spins checking atomic counter, yields to selector, repeats.
4. Job counters are pool-allocated and require explicit `_del` cleanup. Lumix uses stack-local counters — no allocation, no cleanup.
5. `async.task` exists solely because jobs and VFS don't compose. With signals, it's unnecessary.
6. `mel_init()` is a sequential chain of init calls. No parallel startup.
7. VFS takes `Mel_Vfs*` parameter everywhere. Should be global.

### What we want

- Kernel async IO (io_uring on Linux, kqueue on macOS, IOCP on Windows)
- One universal completion primitive (signals) that works for jobs, IO, GPU, network, anything
- Fiber parking on signals instead of busy-yield polling
- Stack-local counters (no pool, no cleanup)
- Global engine-owned job system, async IO, and VFS
- Parallel engine startup via priority constructors
- Zero-copy paths: mmap GPU-native assets, send to GPU directly
- General-purpose: games, servers, CLI tools, database apps, text editors

## Architecture

```
        +---------+  +---------+  +--------+
        |   VFS   |  | Network |  | Direct |
        | (files, |  | (socket,|  | (raw   |
        |  mounts)|  |  http)  |  |  fd)   |
        +----+----+  +----+----+  +---+----+
             |             |          |
             +------+------+----------+
                    |
             +------+------+
             |  Async IO   |
             | (io_uring,  |
             |  kqueue,    |
             |  IOCP)      |
             +------+------+
                    |
             +------+------+
             | Job System  |
             | (signals,   |
             |  fibers,    |
             |  workers)   |
             +-------------+
```

Job system is the foundation. Async IO bridges kernel completions to signals. VFS/Network/anything are consumers of async IO.

## Layer 1: Job System

### Signal

The universal completion primitive. A single `_Atomic(i64)` that packs:
- Lower 16 bits: counter value (0 = green, >0 = red)
- Upper 48 bits: pointer to intrusive linked list of parked fibers

```c
typedef struct {
    _Atomic(i64) state;
    u32 generation;
} Mel_Signal;
```

Operations:

```c
void mel_signal_set(Mel_Signal* s);
```
Turn red. If already red, no-op on counter (sets bit 0). Increments generation.

```c
void mel_signal_clear(Mel_Signal* s);
```
Turn green. Atomically exchanges state to 0, walks the waiting fiber list, pushes each fiber back into work queues. All parked fibers wake.

Thread safety: can be called from any thread, including non-worker threads (completion handlers). When called from a non-worker context, woken fibers are pushed to the global queue.

```c
void mel_signal_wait(Mel_Signal* s);
```
If green (counter == 0), return immediately. Otherwise: spin briefly (40 iterations), then park the current fiber on the signal's waiting list. The thread picks up another fiber and continues working.

Caller must be on a worker fiber. Calling from outside the job system (main thread without fiber context) is undefined — assert in debug.

```c
void mel_signal_wait_and_set(Mel_Signal* s);
```
Wait for green, then atomically turn red. Only one waiter proceeds (mutex semantics). Used to build `Mel_Mutex`.

### Counter

A Signal whose counter is incremented when jobs are dispatched and decremented when jobs complete. When the counter reaches 0, the signal goes green and all parked fibers wake.

```c
typedef struct {
    Mel_Signal signal;
} Mel_Counter;
```

Stack-local. No allocation, no pool, no cleanup. Declare it, use it, let it go out of scope.

```c
void mel_counter_wait(Mel_Counter* c);
```
Equivalent to `mel_signal_wait(&c->signal)`.

Ownership: the counter must outlive all jobs that reference it. In practice this means the scope that creates the counter must wait on it before returning (or the counter must be heap-allocated if the lifetime is longer).

### Mutex

A Signal used as a fiber-aware mutex.

```c
typedef struct {
    Mel_Signal signal;
} Mel_Mutex;

void mel_mutex_enter(Mel_Mutex* m);
void mel_mutex_exit(Mel_Mutex* m);
```

`enter` = `mel_signal_wait_and_set`. `exit` = wake ONE waiting fiber (not all), then turn green.

### Job API

```c
typedef void (*Mel_Job_Fn)(void* data);

void mel_job_run(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, ...);
```

Dispatches a single job. If `on_finish` is non-NULL, the counter is incremented on dispatch and decremented when the job completes. `on_finish` can be NULL for fire-and-forget.

Additional parameters (via _opt pattern):
- `u8 worker` — pin to specific worker (default: MEL_JOB_ANY_WORKER)

```c
void mel_job_run_n(void* data, Mel_Job_Fn fn, Mel_Counter* on_finish, u32 n);
```

Dispatch N jobs with the same function and data. Counter is incremented by N. Jobs share data pointer — use atomics for work distribution (Lumix forEach pattern).

```c
void mel_job_move_to_worker(u8 worker_index);
```

Migrate the current fiber to a different worker thread. The fiber is parked on the current worker and pushed to the target worker's pinned queue. When resumed, the fiber is guaranteed to be on the target worker.

Use case: migrate to IO-dedicated worker for blocking calls (zip decompression, legacy APIs), then migrate back.

```c
void mel_job_yield(void);
```

Yield the current fiber. Equivalent to `mel_job_move_to_worker(MEL_JOB_ANY_WORKER)`.

### Workers and Queues

Three queue tiers (Lumix model):

1. **Work-stealing queue** (per worker) — SPMC lock-free chase-lev deque. Producer pushes/pops from one end, other workers steal from the other. The fast path.
2. **Pinned queue** (per worker) — MPMC lock-free queue (Vyukov bounded MPMC or similar). For jobs that must run on a specific worker. Any thread can push. Fast empty check via atomic flag.
3. **Global queue** (one) — MPMC lock-free queue. For jobs pushed from non-worker threads (outside job system, kernel completion handlers, etc). Lock-free is critical here because kernel completion handlers (mel_aio_drain, dispatch_io callbacks) push woken fibers to this queue — no mutex in the completion path.

Work selection priority:
1. Pinned queue (rare but critical-path)
2. Own WSQ pop (very fast)
3. Steal from other workers' WSQs
4. Global queue (rare)

Sleep model: workers spin briefly looking for work, then go to OS-level sleep via mutex/condvar. `wake()` checks `num_sleeping` (atomic read, fast path when 0) before touching the mutex. Wake is targeted — wake a specific worker or wake any sleeping worker.

### Fiber Pool

Fixed-size pool of fiber stacks, pre-allocated at init. When a fiber parks (signal wait, job completion), it returns to the free pool. When a new job or woken fiber needs execution, a free fiber is popped.

Fiber stacks are allocated via vmem (reserve + commit). Guard page at the bottom for stack overflow detection (existing behavior in `mel_fiber_stack_init`).

Pool size is configurable at init. Default: 512 fibers with 64KB stacks (per Lumix defaults).

### Thread safety summary

- `mel_signal_set/clear` — safe from any thread
- `mel_signal_wait/wait_and_set` — must be on a worker fiber (assert in debug)
- `mel_job_run/run_n` — safe from any thread (non-worker pushes to global queue)
- `mel_job_move_to_worker/yield` — must be on a worker fiber
- `mel_counter_wait` — must be on a worker fiber
- `mel_mutex_enter/exit` — must be on a worker fiber


## Layer 2: Async IO

The kernel async bridge. Abstracts io_uring (Linux), kqueue + dispatch_io (macOS), IOCP (Windows).

### Purpose

Translate kernel IO completions into signal clears. The async IO layer is NOT a user-facing API — it's infrastructure used by VFS, network, and other IO consumers internally.

### Core model

```c
typedef struct {
    void* buf;
    i64   size;
    i64   offset;
    i32   fd;
    Mel_Signal* signal;
    i64*  result;
    i32*  error;
} Mel_Aio_Op;
```

An IO consumer (VFS backend, network layer) fills an `Mel_Aio_Op` and submits it. The async IO layer submits it to the kernel. When the kernel completes the operation, the async IO layer writes the result, writes the error, and calls `mel_signal_clear(op->signal)`.

### Completion draining

Someone must poll the kernel completion queue and process completions.

**Engine frame sync.** The engine calls `mel_aio_drain()` at explicit points in the frame/event loop. Deterministic, debuggable. The application controls exactly when IO completions are processed.

For games: drain at start of frame, after simulation, before render.
For servers: drain in the event loop iteration.
For CLI tools: drain after dispatching IO work.

No drain fiber. The application owns the drain cadence.

```c
void mel_aio_init(void);
void mel_aio_shutdown(void);
i32  mel_aio_drain(void);
void mel_aio_submit(Mel_Aio_Op* op);
```

`mel_aio_drain` returns the number of completions processed. Non-blocking — returns 0 if no completions are ready.

`mel_aio_submit` is thread-safe. Multiple fibers/threads can submit concurrently.

### Platform backends

**Linux (io_uring):**
- `mel_aio_submit` → `io_uring_prep_read/write` + `io_uring_submit`
- `mel_aio_drain` → `io_uring_peek_cqe` loop, process completions, clear signals

**macOS (dispatch_io):**
- `dispatch_io` is Apple's native kernel-level async file IO API, built into GCD. Not a hack or workaround — it's the official path.
- `mel_aio_submit` → `dispatch_io_read/write` with completion block
- Completion block writes result/error, calls `mel_signal_clear(op->signal)`
- `mel_aio_drain` → `kevent` with timeout=0 for socket/pipe completions; dispatch_io completions fire via GCD blocks (may need dispatch queue integration or explicit drain point)
- For sockets/pipes: kqueue directly

**Windows (IOCP):**
- `mel_aio_submit` → `ReadFile/WriteFile` with OVERLAPPED
- `mel_aio_drain` → `GetQueuedCompletionStatusEx` loop

**Fallback (last resort, specific backends only):**
- Every platform Melody targets must have a real kernel async IO path. No "blocking reads on a thread pool pretending to be async" — that's what v1 did and it's the reason we're here.
- If a specific backend genuinely cannot do kernel async (zip decompression is CPU work, not IO), it uses `mel_job_move_to_worker` to migrate to a dedicated IO worker. But this is for specific backends, not the default path.

### What this replaces

- `async.io` — entirely. The SQE/CQE worker thread model, ticket system, handler registry, QoS lanes — all gone.
- The completion-draining responsibility moves from "IO worker threads" to "kernel + drain points."


## Layer 3: VFS

Global singleton. Mounts, backends, capabilities. Synchronous-looking API that internally uses async IO + fiber parking.

### What stays from v1 (concepts, not implementation)

- Mount system concepts (prefix, priority, writable, generation)
- Path normalization rules (forward slash, collapse dots, no root escape)
- Backend vtable concept (open/close/read/write/stat/dir/map/watch/sync/rename/delete/mkdir)
- Status codes (OK, NOT_FOUND, PERMISSION, etc.)
- Test coverage (adapted to new API)

The VFS internals are redesigned from the ground up to fit the signal/fiber model. The v1 implementation (slotmap + mutex + SQE/CQE) is not preserved — the concepts inform the new design but the code is rewritten.

### Execution model

VFS functions are synchronous-looking. They submit IO via `mel_aio_submit`, park the fiber via `mel_signal_wait`, and return when the IO completes. No SQE/CQE queue, no tickets, no polling.

```c
str8 mel_vfs_read_file_alloc(str8 path, Mel_Arena* arena);
```

Inside:
1. Resolve mount, open file via backend
2. Stat for size
3. Allocate buffer from arena
4. Create stack-local signal
5. Submit read via `mel_aio_submit` (backend has `CAP_ASYNC`) or dispatch to IO worker via `mel_job_move_to_worker` (backend without `CAP_ASYNC`, e.g. zip)
6. `mel_signal_wait(&signal)` — fiber parks
7. Return data

Must be called from a worker fiber. Calling from outside a fiber context asserts in debug. There is no silent blocking fallback — if you need IO, you're on a fiber.

### Handle model

File handles are fiber-local. Open on a fiber, use on that fiber, close on that fiber. No mutex on handle lookups. Assert on cross-fiber access in debug builds.

If multiple fibers need to read the same file concurrently, each opens its own handle. This is simpler, faster, and matches what every major engine does.

### Global singleton

The VFS is engine-owned. No `Mel_Vfs*` parameter.

```c
void mel_vfs_mount(str8 prefix, Mel_Vfs_Backend* backend, ...);
void mel_vfs_unmount(str8 prefix);

str8           mel_vfs_read_file_alloc(str8 path, Mel_Arena* arena);
i64            mel_vfs_read(str8 path, void* buf, i64 size, i64 offset);
bool           mel_vfs_write_file(str8 path, str8 data);
bool           mel_vfs_stat(str8 path, Mel_Vfs_Stat* out);
void           mel_vfs_enumerate(str8 path, Mel_Vfs_Enumerate_Cb cb, void* user, ...);

Mel_Vfs_Handle mel_vfs_open(str8 path, u32 flags);
void           mel_vfs_close(Mel_Vfs_Handle fh);
i64            mel_vfs_read_handle(Mel_Vfs_Handle fh, void* buf, i64 size, i64 offset);
i64            mel_vfs_write_handle(Mel_Vfs_Handle fh, const void* buf, i64 size, i64 offset);

Mel_Vfs_Map    mel_vfs_map(Mel_Vfs_Handle fh, i64 offset, i64 size, u32 prot);
void           mel_vfs_unmap(Mel_Vfs_Map map);
void*          mel_vfs_map_ptr(Mel_Vfs_Map map, i64* out_size);
```

### Backend capabilities

Backends declare what they can do:

```c
#define MEL_VFS_CAP_READ       (1u << 0)
#define MEL_VFS_CAP_WRITE      (1u << 1)
#define MEL_VFS_CAP_MMAP       (1u << 2)
#define MEL_VFS_CAP_ASYNC      (1u << 3)
#define MEL_VFS_CAP_WATCH      (1u << 4)
#define MEL_VFS_CAP_SEEK       (1u << 5)
```

Added to the backend vtable:

```c
typedef struct Mel_Vfs_Backend {
    u32 caps;
    // ... existing vtable entries ...
} Mel_Vfs_Backend;
```

The VFS checks caps before choosing execution path:
- `CAP_ASYNC` → submit via `mel_aio_submit`, fiber parks
- No `CAP_ASYNC` → blocking backend call (in current fiber, on current worker)
- `CAP_MMAP` → `mel_vfs_map` delegates to backend
- No `CAP_MMAP` → `mel_vfs_map` returns error (or emulates via read + vmem allocation, TBD)

### Zero-copy GPU path

For pre-baked GPU-native assets:

```c
Mel_Vfs_Handle fh = mel_vfs_open(S8("textures/brick.bin"), MEL_VFS_OPEN_READ);
Mel_Vfs_Map map = mel_vfs_map(fh, 0, 0, MEL_VFS_MAP_READ);

void* data = mel_vfs_map_ptr(map, &size);
// data points at file pages. OS pages in on demand.
// upload to GPU directly from this pointer.
mel_gpu_image_upload_mapped(dev, &image, data, size);

// after GPU fence signals:
mel_vfs_unmap(map);
mel_vfs_close(fh);
```

On Apple Silicon (unified memory), the mapped pointer could be used as a GPU buffer directly — no staging copy needed.


## Engine Init Model

### Priority constructors

Engine subsystems register via `__attribute__((constructor(priority)))`. Lower priority = earlier execution. All constructors run single-threaded before `main()`.

Priority ranges (spaced by 100 for plugin headroom):

```
Priority  100: Logging system (must be first — everything else may log)
Priority  200: Job system (creates workers, they start looking for work)
Priority  300: Async IO layer (sets up io_uring/kqueue/dispatch_io)
Priority  400: VFS (creates global instance, default mounts)
Priority  500: GPU device + swapchain
Priority  600+: Subsystems dispatch init jobs:
  - Sprite pass: mel_job_run(load_sprite_shader, ...)
  - Mesh pass: mel_job_run(load_mesh_shaders, ...)
  - Text pass: mel_job_run(load_text_shader, ...)
  - Texture pool: mel_job_run(init_default_textures, ...)
```

Plugins pick priorities within the gaps (e.g. 650 for a plugin that needs GPU but loads its own shaders). The 100-spacing gives room for future engine subsystems to slot in without renumbering.

By the time subsystem constructors fire at priority 600, the job system (200) is already running with a full thread pool. Init jobs execute in parallel on workers while more constructors fire.

### Startup barrier

A startup counter tracks all init jobs. Each constructor that dispatches init work increments it. Each completed init job decrements it.

The engine provides `main()` (in a .c file linked with the engine). It:
1. (Constructors already ran — logging, jobs, async IO, VFS, GPU, subsystems all initialized)
2. Waits on startup counter (all init jobs complete)
3. Runs user's `mel_main()` on a worker fiber
4. When `mel_main()` returns, begins shutdown

The user defines `int mel_main(void)` and links with melody. No macro, no ceremony. All job/signal/VFS APIs are available from line 1 because the code runs on a fiber.

The user does not call `mel_init()`. The user does not set up threading. The user just writes code.

### Shutdown

Reverse priority. Engine registers `__attribute__((destructor(priority)))` handlers or uses an explicit shutdown sequence triggered when the user's entry point returns.

Shutdown order:
1. User code returns
2. Wait for all outstanding jobs
3. VFS shutdown (assert no open handles)
4. Async IO shutdown
5. Job system shutdown (signal workers to exit, join threads)


## Progress Tracking

For complex loads (levels, materials) that need percentage reporting.

```c
typedef struct {
    Mel_Signal signal;
    _Atomic(i64) completed;
    i64 total;
} Mel_Progress;
```

Usage:

```c
void mel_progress_init(Mel_Progress* p, i64 total);
void mel_progress_advance(Mel_Progress* p, i64 amount);
f32  mel_progress_fraction(Mel_Progress* p);
void mel_progress_wait(Mel_Progress* p);
```

`advance` atomically adds to `completed`. When `completed >= total`, the signal goes green.

`fraction` returns `(f32)completed / (f32)total` — safe to read from any thread (for UI display).

For weighted progress (bytes instead of items), set `total` to total bytes and advance by bytes completed.

Progress composes: a level load creates a progress, sub-loads (materials, meshes) each advance it.


## What Dies

- `async.io.*` — entirely (SQE/CQE worker thread model, handler registry, ticket system)
- `async.task.*` — entirely (DAG orchestrator, step polling)
- `vfs.async.h` — the SQE/CQE API. Replaced by synchronous-looking fiber-yielding functions
- `mel_init()` / `mel_shutdown()` — replaced by constructor/destructor model
- `MEL_APP` / `MEL_CLI` macros — replaced by engine-provided entry point mechanism
- Pool-allocated job counters
- `mel_job_wait_and_del` / `mel_job_test_and_del`
- Semaphore-based worker sleep
- Spinlock-guarded job lists

## What Stays

- `async.fiber.*` — raw context switching primitives (untouched)
- `async.coro.*` — game-loop coroutines (untouched)
- VFS concepts: mount resolution rules, path normalization rules, status codes, backend vtable concept
- VFS test scenarios (rewritten against new API, same coverage intent)

## What's Rewritten

- `async.job.*` — signals, counters, work-stealing queues, fiber parking, `moveJobToWorker`
- VFS execution model — from SQE/CQE/tickets to sync-looking fiber-yielding calls
- Engine init — from `mel_init()` to priority constructors

## Resolved Decisions

1. **macOS file async.** `dispatch_io` is Apple's native kernel-level async file IO. Not a hack — it's the official path. Use it.

2. **Signal from non-worker context.** Global queue and pinned queues are lock-free (Vyukov bounded MPMC or similar). No mutex in the completion path. `mel_signal_clear` from any context pushes woken fibers to the lock-free global queue.

3. **Stack-local counter lifetime.** Debug builds assert counter value == 0 when the counter goes out of scope. Controlled via `MEL_JOB_DEBUG_COUNTER_LIFETIME` in `async.job.cfg.h` (default: on in debug, off in release).

4. **Drain fiber vs frame sync.** Frame sync only. `mel_aio_drain()` at explicit application-controlled points. No drain fiber. The application owns the drain cadence.

5. **VFS handle thread affinity.** Handles are fiber-local. Open, use, close on the same fiber. No mutex on lookups. Assert on cross-fiber access in debug. Multiple fibers wanting the same file open separate handles.

7. **Constructor priority allocation.** Spaced by 100. Logging at 100, job system at 200, async IO at 300, VFS at 400, GPU at 500, subsystems at 600+. Plugins pick priorities in the gaps.

8. **User entry point.** Engine provides `main()`. User defines `int mel_main(void)`. Engine's main waits for startup barrier, runs `mel_main()` on a worker fiber, shuts down on return.

## Open Questions

1. **Memory-mapped file lifetime.** When a mapped region is used for GPU upload, the mapping must stay alive until the GPU fence signals. Who tracks this? The caller? An engine-level resource lifecycle manager?

2. **dispatch_io + mel_aio_drain integration.** dispatch_io completions fire via GCD dispatch queues (callback-based), not a pollable completion queue. How does this integrate with the explicit `mel_aio_drain()` model? Options: (a) dispatch_io callbacks push to a lock-free queue, `mel_aio_drain` processes that queue; (b) use a dedicated serial dispatch queue and drain it explicitly.

3. **Server event loop.** A server has no frame loop. Where does `mel_aio_drain()` get called? The user writes their own event loop that calls drain? Or the engine provides a generic event loop primitive?
