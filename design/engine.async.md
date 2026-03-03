# Async & Memory Architecture

## Two Async Models

Melody has two separate async systems. They coexist, serve different purposes,
and share no code.

## Status

This document mixes implemented systems with target architecture.

Implemented today:
- `melody/async.job.*`
- `melody/async.io.*`

Target (not implemented yet):
- `Mel_Linear_Alloc arenas[]` owned by `Mel_Window` and `Mel_Simulation`
- `mel_window_alloc` and `mel_sim_alloc` convenience APIs

### Mel_Job_Context — Fiber-Based Compute

For CPU-parallel work: physics, pathfinding, culling, procedural generation.

- Each job is a **fiber** (lightweight context switch, not a thread)
- Worker threads run a selector loop: pop job → switch to fiber → job completes → next
- Jobs can **yield** to other jobs (fiber-aware waiting)
- Range dispatch: `mel_job_dispatch(ctx, 1000, callback, user)` splits work across workers
- Priority queues: HIGH, NORMAL, LOW
- Thread tags: restrict jobs to specific workers (e.g. "GPU", "audio")

```c
void physics_update(Mel_Job_Context* jobs, f32 dt) {
    mel_job_dispatch(jobs, entity_count, integrate_bodies, &world);
    mel_job_wait_and_del(jobs, counter);  // yields fiber, doesn't block thread
}
```

Already implemented: `melody/async.job.h`, `melody/async.fiber.h`.

### Mel_Io — Ring-Based IO

For file/network operations: reads, writes, directory enumeration, file watching.

- **SQE/CQE model**: submit queue entries, poll/wait for completion queue entries
- **3 QoS lanes**: latency-critical, streaming, bulk (prevents starvation)
- **Handler registry**: each subsystem registers an execute function by handler_id
- **Ticket-based tracking**: every submission gets a unique u64 ticket
- **LINK_NEXT chaining**: atomic chain admission, failure cascades to linked ops
- **Dual mode**: with workers (async) or without (synchronous on caller thread)

```c
mel_io_submit(&io, sqe_array, count);
u32 completed = mel_io_poll(&io, cqe_buf, max_cqes);
Mel_Io_Cqe cqe = {0};
mel_io_wait_ticket(&io, ticket, timeout_ms, &cqe);
```

Already implemented: `melody/async.io.h`.

### VFS Uses IO

The VFS registers a handler with the IO system. When you submit a VFS operation,
it becomes an IO SQE that runs on an IO worker thread:

```
mel_vfs_submit() → Mel_Io_Sqe{handler=VFS} → IO worker → backend dispatch → CQE
```

VFS copies paths on submission (caller doesn't need to keep them alive).
High-level wrappers (`mel_vfs_read_file_alloc`) submit + wait internally.

---

## Frame Arenas (N-Buffered)

Per-frame linear allocator. Resets every frame with a single pointer assignment.

### Two Arena Types

To decouple **Game Logic Frequency** (fixed step) from **Rendering Frequency** (variable step), we use two distinct arena scopes:

1. **Window Arena (Per-Window)**: Tied to swapchain fences. Stores render lists, UI layout, draw commands. Resets on `mel_window_begin_frame`.
2. **Simulation Arena (Per-Simulation)**: Tied to simulation tick. Stores physics queries, pathfinding scratch, temporary ECS results. Resets each tick. The simulation owns this arena — worlds and game code borrow it during a tick.

### Why N-Buffered?

To prevent the CPU from overwriting Frame N's data while the GPU is still executing it, each scope maintains: `Mel_Linear_Alloc arenas[MEL_MAX_FRAMES_IN_FLIGHT]`.

`mel_frame_alloc` automatically uses the arena for the current frame index of the context (Window or Simulation). The arena is only reset when safe (GPU idle for Window, tick complete for Simulation).

### Usage

```c
void* mel_window_alloc(Mel_Window* window, usize size);
void* mel_sim_alloc(Mel_Simulation* sim, usize size);
```

### Backing

Backed by virtual memory (vmem reserve + commit):
- Reserve a large virtual range at init (e.g. 64MB)
- Commit pages on demand as cursor advances
- Decommit excess pages periodically (or never — committed but unused pages
  don't cost physical RAM on modern OSes until touched)
- Assert if cursor hits reserve ceiling

This means the arena never reallocates, never fragments, and overflow is
a hard assert (you're allocating too much per frame — fix your code).

---

## Threading Model

### Worker Pools

Two separate pools, never mixed:

**Job workers**: run fibers. CPU-bound. Count = max(1, num_cores - 3).
**IO workers**: run blocking syscalls (pread, readdir, etc). Count = 2.

The -3 accounts for: main thread + 2 IO workers. Adjust via CVar or init descriptor.

### Main Thread Responsibilities

- Frame loop (acquire, dispatch, present)
- Render list sorting + upload
- Render graph execution
- ECS observer callbacks
- Draw API calls
- Input polling
- ImGui

### What Runs On Job Workers

- Physics integration
- Pathfinding
- Procedural generation
- Culling (if CPU-driven)
- Anything the game dispatches via `mel_job_dispatch`

### What Runs On IO Workers

- File reads/writes (via VFS → IO handler)
- Directory enumeration
- File watching (kqueue/inotify polling)
- Future: network IO

### Render List Thread Safety

Render lists are **NOT thread-safe** by default. All render list pushes happen on
the main thread. If job fibers need to produce render data, they write to
thread-local staging buffers, which the main thread merges before graph execution.

This keeps the common path (main-thread rendering) zero-overhead while still
allowing parallel content generation when needed.

---

## Open Questions

1. **Frame arena size**: how large is the virtual reserve? 64MB? 256MB?
   Depends on worst-case per-frame allocation (lots of particles, big scenes).
   Should be configurable via CVar or init descriptor.

2. **IO worker count**: fixed at 2, or scale with workload? Most IO is disk-bound,
   not CPU-bound, so more than 2-3 workers gives diminishing returns.

3. **Job system integration with render lists**: thread-local staging is the proposed
   model. Alternative: lock-free concurrent push (more complex, possibly faster for
   heavy parallel content generation). Start with staging, profile, optimize if needed.
