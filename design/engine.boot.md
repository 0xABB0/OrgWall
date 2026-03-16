# Engine Boot System

## Problem

`mel_init()` is a monolithic function that sequentially initializes every engine subsystem:
tracking allocator, GPU device, Slang, sprite pass, text pass, mesh pass, texture pool, font pool.
Adding new subsystems means editing `mel_init()` and `mel_shutdown()`. Shader compilation (3 passes)
runs sequentially when it could run in parallel. The init order is hardcoded, not declarative.

## Decisions

- `mel_init()` dies. `mel_boot()` replaces it.
- Boot runs as a **job** on a worker fiber (can use `mel_counter_wait`).
- `app_init()` runs inside the boot job (on worker fiber) — app gets full async API from the start.
- `app_shutdown()` also runs as a job for symmetric async cleanup.
- Main thread waits on SDL_Semaphore for boot completion.
- Event channels are per-subsystem, defined in each subsystem's header.
- Event channels are made **thread-safe** before building on them (prerequisite work).
- Each phase has its own counter, passed via event payload.
- Tracking allocator is an internal debug detail, not a boot phase. Apps use `mel_alloc_heap()`.
- `mel_allocator()` accessor dies with `mel_init()`. Engine internals use `mel_alloc_heap()` directly.
- Each render pass fires its own `_ready` event (not a single combined event).
- Texture pool subscribes to all 3 pass ready events, uses atomic countdown.
- Event system channels are initialized in constructors.
- `MEL_APP` / `MEL_CLI` macros go away. Replaced by compilation options.
- Legacy app path (`mel__legacy_app_present`) is nuked.
- `Mel_App_Config` goes away in the ideal end state (compile-time defines replace it).
- Constructor = init channel + subscribe. Boot job = do actual work + fire.
- Slang thread safety must be verified before assuming parallel shader compilation.

## Prerequisites

### Event Channel Thread Safety

The current event channel is single-threaded. Before building the boot system on it, it must
be made thread-safe. Boot events fire from worker fibers (potentially different workers for
different channels), and runtime events will fire from arbitrary threads.

Changes to `Mel_Event_Channel`:

```c
struct Mel_Event_Channel {
    Mel_Array(Mel_Event_Channel_Entry) subs;
    SDL_Mutex* mutex;
    u32 next_id;
};
```

**`mel_event_channel_on()` / `mel_event_channel_off()`**: Lock mutex, modify subscriber list, unlock.

**`mel_event_channel_fire()`**: Lock mutex, snapshot the subscriber array to a stack-local copy
(or heap copy if too large), unlock, iterate the snapshot and call listeners. This allows:
- Listeners to call `on()`/`off()` on the same channel without deadlock
- Concurrent fires on different channels (no shared state)
- Listeners to dispatch jobs that eventually fire other channels

The old `firing` bool and its `assert(!ch->firing)` go away. The snapshot-and-release pattern
replaces the reentrance guard with correct concurrent behavior.

**Cost**: One mutex lock/unlock per fire (uncontended during boot = ~25ns on modern hardware).
One stack copy of the subscriber array (typically <10 entries = <160 bytes). Acceptable.

### Slang Thread Safety — VERIFIED UNSAFE

**Finding:** Slang maintainer (csyonghe, Jan 2025, GitHub Discussion #6132) confirmed:
"At the moment we don't support calling global session functions concurrently."
"You will still need to create one global session per thread."

Cross-session component mixing also crashes (Issue #8437). Per-thread sessions cost 0.18-0.5s
each to create (stdlib/IR loading), making a session pool impractical for boot-time parallelism.

**Solution: Pinned Slang worker + parallel Vulkan pipeline creation.**

Only the Slang compilation step (source → SPIR-V) must be serialized. Once SPIR-V is produced,
all downstream Vulkan work (pipeline creation, descriptor allocation, buffer setup) is thread-safe
per the Vulkan spec and can run on any worker in parallel.

Shader compilation jobs use `mel_job_move_to_worker(SLANG_WORKER)` for the compile step only,
then `mel_job_yield()` to return to any available worker for pipeline creation. The Slang worker
processes compilations one-at-a-time (fibers are cooperative, no preemption). No mutex needed.

```
shader_compile_job (on worker X):
    u8 home = mel_job_current_worker();
    mel_job_move_to_worker(SLANG_WORKER);
    spirv = slang_compile(source);           // pinned, serialized
    mel_job_move_to_worker(home);            // back to original worker
    vkCreateGraphicsPipelines(spirv, ...);   // thread-safe, parallel on worker X
    allocate_descriptors(...);               // thread-safe, parallel
    fire(pass_ready);                        // triggers cascade
```

**Option C (pre-compile offline) is impractical.** The engine targets multiple graphics APIs
(Vulkan, DirectX, Metal, WebGPU). Pre-compiling means shipping separate bytecode per API
(SPIR-V, DXIL, Metal IR, WGSL) — a build matrix nightmare. Slang's value IS cross-API
runtime compilation. The pinned worker pattern is the long-term solution, not a stopgap.

## Design

Event-driven boot. Subsystems declare dependencies via event subscriptions (set up in constructors).
The boot sequence runs as a job, firing events as milestones are reached. Listeners react by
dispatching their own init work as parallel jobs.

### Event Channels (Per-Subsystem)

Each subsystem exports its own event channel and event payload type in its own header.

```c
// gpu.device.h
typedef struct {
    Mel_Gpu_Device* dev;
    Mel_Counter* phase_counter;
} Mel_Gpu_Ready_Event;
extern Mel_Event_Channel mel_gpu_device_ready;

// gpu.shader.h
typedef struct {
    Mel_Counter* phase_counter;
} Mel_Slang_Ready_Event;
extern Mel_Event_Channel mel_slang_ready;

// sprite.pass.h
extern Mel_Event_Channel mel_sprite_pass_ready;

// text.pass.h
extern Mel_Event_Channel mel_text_pass_ready;

// mesh.pass.h
extern Mel_Event_Channel mel_mesh_pass_ready;

// texture.pool.h
extern Mel_Event_Channel mel_texture_pool_ready;

// font.atlas.h
extern Mel_Event_Channel mel_font_pool_ready;
```

### Constructor Phase (pre-main)

**IMPORTANT: macOS/ld64 does NOT honor `__attribute__((constructor(N)))` priorities across
translation units in static libraries.** Constructors run in object-resolution order, not
priority order. Verified empirically: `constructor(300)` in a.o runs before `constructor(100)`
in b.o if a.o is resolved first. This means cross-TU constructor dependencies are unreliable.

**Solution: Two-phase init.**
1. Constructors (any order): each subsystem inits its OWN event channel only.
2. Wire callbacks: each constructor registers a "wire" function via `mel__boot_register_wire()`.
   These wire functions subscribe to other subsystems' channels.
3. `mel__boot_run_wires()` is called from `mel_boot()` AFTER all constructors have run
   (post-main). This is when cross-channel subscriptions are safe.

```c
// In sprite.pass.c
static void mel__sprite_pass_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__sprite_pass_on_gpu_ready, nullptr);
}

__attribute__((constructor))
static void mel__sprite_pass_register(void)
{
    mel_event_channel_init(&mel_sprite_pass_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__sprite_pass_wire);
}
```

### Boot Registry (`boot.registry.h`)

Static array of wire callbacks, filled by constructors, executed by `mel_boot()`.
Pre-main registration is single-threaded, no synchronization needed.

```c
void mel__boot_register_wire(Mel_Boot_Wire_Fn fn);
void mel__boot_run_wires(void);
u32  mel__boot_wire_count(void);
```

### Boot Job (post-SDL_Init)

Main thread dispatches the boot job and waits. `app_init()` runs inside the boot job
on a worker fiber, so the app has full access to fiber-aware async APIs (`mel_counter_wait`,
`mel_signal_wait`, async file loads, etc.) from the very start.

```c
static SDL_Semaphore* s_boot_sem;

static void mel__boot_job(void* data)
{
    // --- Phase 1: GPU + Slang (sequential on this fiber) ---
    mel_gpu_device_init(&s_dev, ...);
    mel_slang_init();

    // --- Phase 2: Fire gpu_ready, triggering parallel shader compilation ---
    Mel_Counter shader_counter = {0};
    mel_event_channel_fire(&mel_gpu_device_ready, &(Mel_Gpu_Ready_Event){
        .dev = &s_dev,
        .phase_counter = &shader_counter,
    });

    // --- Phase 3: Wait for all shader compilation jobs ---
    mel_counter_wait(&shader_counter);

    // At this point, each pass has:
    //   1. Compiled its shaders (inside the job)
    //   2. Fired its own _ready event (inside the job, before counter decrement)
    //   3. Cascaded: texture_pool and font_pool have already initialized
    //      via the event chain (see "Cascade" section below)

    // --- Phase 4: App init (on worker fiber — full async API available) ---
    app_init();

    // --- Phase 5: Signal main thread ---
    SDL_SignalSemaphore(s_boot_sem);
}

void mel_boot(void)
{
    s_boot_sem = SDL_CreateSemaphore(0);
    mel_job_run(nullptr, mel__boot_job, nullptr);
    SDL_WaitSemaphore(s_boot_sem);
    SDL_DestroySemaphore(s_boot_sem);
    s_boot_sem = nullptr;
}
```

### Listener Pattern (Shader Compilation)

Each pass subscribes to `mel_gpu_device_ready`. When fired, dispatches a shader compilation
job using the phase counter from the event payload. `mel_job_run` auto-increments/decrements
the counter.

The job splits into two phases: Slang compilation (pinned to SLANG_WORKER) and Vulkan
pipeline creation (any worker, parallel). Only the source→SPIR-V step is serialized.

```c
static Mel_Gpu_Device* s_dev_ref;

static void mel__sprite_pass_on_gpu_ready(void* ctx, const void* event)
{
    const Mel_Gpu_Ready_Event* e = event;
    s_dev_ref = e->dev;
    mel_job_run(nullptr, mel__sprite_pass_compile, e->phase_counter);
}

static void mel__sprite_pass_compile(void* data)
{
    u8 home = mel_job_current_worker();

    mel_job_move_to_worker(MEL_SLANG_WORKER);
    Mel_Gpu_Shader_Bytecode bytecode = mel_gpu_shader_compile(...);

    mel_job_move_to_worker(home);
    mel_sprite_pass_init_from_bytecode(s_sprite_pass, .dev = s_dev_ref, .bytecode = &bytecode, ...);

    mel_event_channel_fire(&mel_sprite_pass_ready, nullptr);
    // counter auto-decrements AFTER this function returns
    // so all downstream event cascades happen before the decrement
}
```

### Cascade: Pass Ready -> Pool Init

Texture pool subscribes to all 3 pass ready events. Uses an atomic countdown to know when
all 3 have fired. The last one to fire triggers texture pool init.

```c
static _Atomic(i32) s_passes_remaining;

__attribute__((constructor(300)))
static void mel__texture_pool_register(void)
{
    mel_event_channel_init(&mel_texture_pool_ready, mel_alloc_heap());
    atomic_store(&s_passes_remaining, 3);
    mel_event_channel_on(&mel_sprite_pass_ready, mel__texture_pool_on_pass_ready, nullptr);
    mel_event_channel_on(&mel_text_pass_ready, mel__texture_pool_on_pass_ready, nullptr);
    mel_event_channel_on(&mel_mesh_pass_ready, mel__texture_pool_on_pass_ready, nullptr);
}

static void mel__texture_pool_on_pass_ready(void* ctx, const void* event)
{
    if (atomic_fetch_sub(&s_passes_remaining, 1) == 1)
    {
        mel_texture_pool_init(s_texture_pool, ...);
        mel_event_channel_fire(&mel_texture_pool_ready, nullptr);
    }
}
```

Font pool subscribes to `mel_texture_pool_ready` and inits synchronously:

```c
static void mel__font_pool_on_texture_pool_ready(void* ctx, const void* event)
{
    mel_font_atlas_pool_init(s_font_pool, ...);
    mel_event_channel_fire(&mel_font_pool_ready, nullptr);
}
```

### Timing Proof

Why everything is done by the time `mel_counter_wait` returns:

1. Boot job fires `gpu_device_ready` with `shader_counter`
2. Three listeners each call `mel_job_run(..., shader_counter)` — counter goes to 3
3. Boot job calls `mel_counter_wait(&shader_counter)` — parks
4. Worker A finishes sprite compile → fires `sprite_pass_ready` → texture_pool atomic 3→2
5. Worker B finishes text compile → fires `text_pass_ready` → texture_pool atomic 2→1
6. Worker C finishes mesh compile → fires `mesh_pass_ready` → texture_pool atomic 1→0 →
   texture_pool inits → fires `texture_pool_ready` → font_pool inits → fires `font_pool_ready`
7. Workers A/B/C return from job fn → counter decrements → eventually hits 0
8. Boot job wakes, calls `app_init()`, signals main thread

Step 6 happens INSIDE the job function (before the counter decrement in step 7).
So by the time the counter reaches 0, the entire downstream cascade is complete.

### Dependency Graph

```
                        mel_boot_job (fiber)
                            |
                    GPU device + Slang
                            |
                    fire(gpu_device_ready)
                     /      |       \
              sprite_pass  text_pass  mesh_pass   << parallel shader compilation >>
                  |           |           |
              fire(own      fire(own    fire(own
              _ready)       _ready)     _ready)
                  \           |           /
              texture_pool (atomic countdown, last one triggers init)
                            |
                    fire(texture_pool_ready)
                            |
                       font_pool (synchronous init)
                            |
                    fire(font_pool_ready)
                            |
              ---- all inside job functions, before counter decrement ----
                            |
                    shader_counter → 0
                            |
                    boot job wakes
                            |
                    app_init() (on worker fiber)
                            |
                    SDL_SignalSemaphore
                            |
                    main thread resumes
                            |
                    SDL_AppIterate loop begins
```

### Compilation Options

`MEL_APP` and `MEL_CLI` macros are removed. The engine mode is controlled by compilation defines:

```
MEL_ENABLE_GPU        - compile GPU device, shader compilation, render passes
MEL_ENABLE_WINDOW     - compile SDL window/event system
MEL_ENABLE_RENDER     - compile render graph, techniques, stages (implies GPU)
MEL_ENABLE_IMGUI      - compile imgui integration (implies RENDER + WINDOW)
```

A CLI tool compiles with none of these. It gets: allocator, VFS, jobs, AIO, logging,
collections, math, strings. No SDL_Init, no boot job, no GPU.

A GUI app compiles with `MEL_ENABLE_GPU + MEL_ENABLE_WINDOW + MEL_ENABLE_RENDER`.

The constructor-based architecture makes this clean: if sprite.pass.c isn't compiled,
its constructor doesn't run, it doesn't subscribe to `gpu_device_ready`, the boot job
fires the event to zero listeners, and the counter stays at 0. The system self-configures
based on what's linked.

SDL owns `main()` via `SDL_MAIN_USE_CALLBACKS`. The engine owns the SDL callbacks
(`core.app.callbacks.c` + `core.app.entry.c`). The application only implements `app_init()`,
`app_shutdown()`, `app_event()`. The app never touches the entry point.

Compilation options control what the engine does before calling `app_init()`:
- With `MEL_ENABLE_GPU`: engine boots GPU, compiles shaders, inits pools, then calls `app_init()`
- Without: engine skips GPU boot, calls `app_init()` directly with only jobs/VFS/AIO available

For CLI apps (no window, no GPU), the engine still owns the entry point but skips SDL video
init and the GPU boot phases entirely. The app implements the same `app_init()` signature.

### Configuration

`Mel_App_Config` is replaced by compile-time defines:

```
MEL_GPU_VALIDATION      - enable Vulkan validation layers (default: 1 in debug, 0 in release)
MEL_MAX_FRAME_TIME      - frame time clamp (default: 0.25)
MEL_APP_NAME            - application name string (default: argv[0])
```

### mel_frame() and the Main Thread

`mel_frame()` runs from `SDL_AppIterate` on the main thread. It calls sim ticks, render
graph execute, etc. User code in sim callbacks runs on the main thread and CANNOT use
fiber-aware APIs (`mel_counter_wait`, `mel_signal_wait`). This is a design constraint.

If game code needs async work during a frame, it dispatches jobs and reads results next frame
(or in a later sim tick). The frame loop is NOT a fiber. This is intentional — the main thread
owns the SDL event pump and the render graph submission, both of which must stay on one thread.

### Shutdown

Reverse cascade, also runs as a job for symmetric async cleanup:

```c
static void mel__shutdown_job(void* data)
{
    app_shutdown();  // app cleanup on worker fiber

    vkDeviceWaitIdle(s_dev.device);

    fire(mel_shutdown_begin)
      -> font_pool shutdown
      -> texture_pool shutdown
      -> pass shutdowns
      -> slang shutdown
      -> GPU device shutdown
}
```

Each subsystem subscribes to `mel_shutdown_begin` in its constructor. Ordering via
priority or explicit "shutdown after X" dependencies. Destructors (`__attribute__((destructor))`)
handle the data structure cleanup (registries, slotmaps — same as now).

### Init Functions Must Be Thread-Safe

Pass compilation jobs run on different workers simultaneously. Their init functions
(`mel_sprite_pass_init`, `mel_text_pass_init`, `mel_mesh_pass_init`) must be safe to
call from any worker thread. This means:
- `mel_alloc_heap()` (malloc) — thread-safe
- `vkCreateGraphicsPipelines` — thread-safe per Vulkan spec
- Descriptor allocation — thread-safe with separate pools
- Slang compilation — MUST BE VERIFIED (see Prerequisites)

Texture pool init runs from whichever worker fires the last pass_ready event.
Font pool init runs from the same cascading call. These must also be thread-safe.

### Sticky Events (Future)

Boot events fire exactly once. If a subsystem registers after the event has already fired
(e.g., a plugin loaded later), it misses the event. This is fine for boot (constructors
run before boot job). For future plugin/hot-reload support, a "sticky" channel variant
that replays the last fired event to new subscribers would be needed.

Not needed now. Noted for future.

## Migration Path

Phase 0: Make event channels thread-safe
Phase 1: Verify Slang thread safety for concurrent compilation
Phase 2: Event channels on GPU, slang, passes, pools (constructors)
Phase 3: Boot job replaces mel_init(), app_init() runs inside boot job
Phase 4: Pass compilation as parallel jobs
Phase 5: Nuke legacy app path, mel_init_opt, Mel_Init_Opt, mel_allocator()
Phase 6: Compilation options (MEL_ENABLE_*)
Phase 7: Nuke MEL_APP / MEL_CLI macros
