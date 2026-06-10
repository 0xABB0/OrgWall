# main_design (probe)

The framework composes **control inversion** — who owns the CPU on each OS thread and how it is
relinquished — not a loop, not a scheduler, not a proactor. `GetMessage`, `PeekMessage`,
`NSApplication.run`, and a DOM-driven page are the shapes this composes — sovereign points on one
`min`-deadline spectrum, or the subordinate regime adjoined to it — *selected* from what the
application registered and slid along at runtime. The application declares its concerns; the
idiomatic per-platform loop, or its absence, is synthesized (MEL-ENGINE-II, V, VII).

## A powerful instrument, not a nanny

The framework hands the user a maximally powerful tool and does not prevent misuse
(MEL-ENGINE-II, IV, V). Async I/O, thread affinity, and replaceable drivers are supplied; a user who
writes blocking I/O and queues it on an executor — ignoring the async I/O given for free — shoots his
own foot, and that is his prerogative. The framework's contract is narrower and stricter: the
substrate is correct, every failure is loud (MEL-ENGINE-VIII), and every policy is replaceable
(MEL-ENGINE-IX, IV). It guards correctness, never taste.

## Concepts

The vocabulary, exact. Each is an abstraction (a vtbl), a value, or a platform primitive.

- **Vat** — one OS thread as the unit of control and affinity; hosts a driver, sources, and work. A
  value (open/close). The actor-model term: a thread of control with a turn queue.
- **Sovereignty** — whether a vat owns its thread (*sovereign*: it calls the OS) or is entered by it
  (*subordinate*: the OS calls it). Decides driver-vs-bridge.
- **Driver** — the vtbl that runs a sovereign vat's turns; *fair* and *unfair* are implementations,
  chosen by handing the vat the driver object — no flag.
- **Turn** — one driver iteration: reduce demands → wait → drain ready sources → run ready work.
- **Relinquishment** — how a sovereign vat yields the CPU: spin / timed / indefinite.
- **Retention** — what keeps a vat live (any source or outstanding awaited work); its absence is the exit.
- **Source** — the vtbl integrating one OS-delivered concern into a vat: four entries — `wakeables`,
  `deadline`, `drain`, `cancel` — and nothing more. It is not a GSource; features do not accrete here.
- **Waiter** — a platform's sovereign wait primitive: block/poll a wakeable set with one timeout.
- **Bridge** — a platform's subordinate primitive: inject a wakeable into the OS loop.
- **Wakeable** — an opaque platform handle a source adds to the wait set (an fd, a HANDLE, the message
  queue). A source contributes a set of them (possibly empty) plus a deadline — no kind tag.
- **Demand** — a source's per-turn assertion: its wakeables + a deadline (`0` / `t` / `MEL_NEVER`).
  The driver reduces demands by `union(wakeables)` and `min(deadlines)`.
- **Doorbell** — the coalescing wakeup one vat rings on another (auto-reset event, eventfd, mach port,
  `postMessage`).
- **Mailbox** — a vat's thread-safe incoming queue, drained to empty each wake; the doorbell only
  wakes. An abstraction with implementations, not a capacity policy — a bounded ring (backpressures),
  an unbounded list (allocates), a reserved-slot ring (completions never fail): the vat is handed the
  one it wants, like its driver.
- **Task** — run-to-completion work, cancellable.
- **Fiber** — stackful work that *suspends* (never blocks) by switching stacks, resumed via doorbell/mailbox.
- **Coroutine** — stackless work that *suspends* by returning to its caller (a state-machine, via the transform).
- **Await** — the idea of suspending work until a completion, realized by a fiber suspend, a coroutine
  suspend, or a blocking wait; the mechanism is the primitive.
- **Executor** — N worker threads running tasks behind a shared mailbox; its tasks have no thread
  affinity (any worker runs them). Distinct from a vat (one thread, a driver, real affinity).
- **Cancellation** — a request, not a guarantee; the op may still complete and is still reaped.
- **Operation** — a submitted async OS request: read / write / accept / connect / timeout.
- **Completion** — the event that an operation finished.
- **Result** — the value-or-error a completion or await yields. *(its error channel: owed.)*
- **Buffer ownership** — an operation's buffer must outlive it. *(owed.)*

**Source disciplines** — how a source is serviced, selected per platform, never the architecture:

- **Proactor** — submit an Operation, receive a Completion; the OS does the I/O. Native on a completion
  Waiter (io_uring, IOCP).
- **Reactor** — the Waiter reports readiness; the source's `drain` does the syscall. Native on a
  readiness Waiter (epoll, kqueue, `MsgWait`). May present the proactor surface by synthesizing the
  Completion on readiness.
- **Inverted (push)** — the subordinate case: the OS calls the source (`WndProc`, DOM,
  `NSApplication`, a camera callback). Neither reactor nor proactor.
- **Executor offload** — the realization for objects readiness cannot represent (regular files): a
  worker performs the blocking syscall and rings the doorbell.

A sovereign vat's sources are reactors or proactors (readiness vs completion Waiter); a subordinate
vat's are inverted. The engine is not "the proactor" — the proactor is one discipline of one source.

## The spectrum of relinquishment

A thread either calls the OS (**sovereign**) or is called by it (**subordinate**):

- sovereign, never sleep — poll, run, loop. `PeekMessage` spin; maximum frame rate.
- sovereign, sleep until a deadline — timed wait. A throttled frame; a timer.
- sovereign, sleep until an event — indefinite wait. `GetMessage`, `epoll_wait`; an idle GUI, a server.
- subordinate — the OS owns the CPU and calls us. `NSApplication.run`, a `WndProc`, a DOM listener, an
  OS camera callback.

The first three are one continuum — the `min`-deadline reduction interpolates them. Subordinate is not
on that axis: no driver, OS-bridged — a categorically different regime *adjoined* to the spectrum, not
a point on it. The point is chosen per thread from the registered concerns and recomputed whenever they
change. A game that minimizes slides from spin to indefinite wait with no application code.

## Concerns, held separate

### Platform substrate
Per platform, two primitives and a set of *wakeable* kinds. The **Waiter** (sovereign): block-or-poll
a *set* of wakeables with one timeout — `MsgWaitForMultipleObjectsEx`+`QS_ALLINPUT`,
`epoll_wait`/`io_uring_enter`, `kqueue`, `WaitForMultipleObjects`. The **Bridge** (subordinate):
inject a wakeable into an OS-owned loop — `CFRunLoopSource`, a DOM listener, an added `HANDLE`.
A source contributes a *set* of platform wakeable-handles (possibly empty) and a deadline — there is
no kind tag at the source surface: "no wakeable" is the empty set, a poll-only source is the empty set
with a finite deadline. What a handle *is* (an fd, a HANDLE, the message queue) is the Waiter's
per-platform concern, not an enumeration the source declares. One handle is distinguished — the
**doorbell**, a coalescing event (Win32 auto-reset event, Linux eventfd, darwin mach port, wasm
`postMessage`) by which one vat wakes another; the payload crosses on the receiving vat's **mailbox**,
the doorbell carrying only the wake.

### Vat — a thread as a control domain
A vat is one OS thread, **sovereign** or **subordinate** — one thread only, so its single Waiter and
the affinity claim below are literal. Vats are values, created as needed — a camera needing its own
poll thread opens one — but a vat *is* a thread, with a thread's creation cost (MEL-ENGINE-VI): open
them deliberately and reuse for churn, never per-frame.

    Mel_Vat *mel_vat_open (Mel_Allocator*, Mel_VatDesc);
    Mel_Vat *mel_vat_self (void);
    void     mel_vat_close(Mel_Vat*);

Work and sources are *bound to* a vat — that binding is thread affinity. A thread waits on one
primitive at a time, so a vat's sources are reduced together; that is physical, not central ownership.
A source is bound to its vat for life; it does not migrate. `mel_vat_alloc(v)` yields the vat's
allocator — its thread-safety under cross-vat use is the allocator's contract, not the vat's.
The platform `main` creates the **root** vat (sovereign or subordinate per platform) and either runs
its driver or returns to the OS.

### Source — an OS-delivery integration
The unit of "a concern the OS services," bound to a vat. A source contributes, each turn:

- its **wakeables** — what to add to the wait set (or none, for a pure callback),
- its **deadline** in ns — when it next wants the CPU regardless of wakeables: `0` = do not sleep,
  `MEL_NEVER` = only on a wakeable,

and is **drained** when a wakeable fires or its deadline elapses. A source is a vtbl of exactly four
entries — `wakeables`, `deadline`, `drain(budget) → more?`, `cancel` — that the application or a module
authors: the single extension point (MEL-ENGINE-IV, IX). `drain` is *cooperatively bounded*: it
processes at most `budget` units and reports whether work remains, so the driver can round-robin
instead of being trapped in one unbounded drain — fairness rests on this contract, not on the driver
alone. The framework ships the common ones (UI events, frame,
listener, file I/O) but they are not privileged, and the vtbl is never a place to accrete features.

    Mel_Source *mel_source_open (Mel_Vat*, const Mel_Source_Vtbl*, void *state);
    void        mel_source_close(Mel_Source*);

Discipline (reactor / proactor / inverted) is *not a kind* the source declares — it is whatever the
vtbl does against its vat's Waiter. On a subordinate vat the same vtbl is driven by the OS Bridge
instead of the driver.

### The driver — the negotiation
For a sovereign vat, each turn: collect the union of active sources' wakeables and the minimum of
their deadlines; call the **Waiter** with `timeout = min_deadline − now` (clamped ≥ 0; `MEL_NEVER` ⇒
block indefinitely); drain every source whose wakeable fired or deadline elapsed. The three-way
relinquishment is one reduction — `min` over deadlines — not a tag:

- any source at `0` ⇒ timeout 0 ⇒ poll, never sleep.
- a finite minimum ⇒ sleep until it.
- all `MEL_NEVER` ⇒ block until a wakeable.

Ordering and per-source budget *among* ready sources is not a flag inside one driver — it is the
**driver** itself, an abstraction (a vtbl, like a source) with distinct implementations. The framework
ships a fair driver (round-robins ready sources, handing each its `drain` budget — see the source
contract above — so a socket flood cannot starve the frame) and an unfair one (drains greedily for throughput); an application supplies its own by
authoring the vtbl. A vat is handed the driver it runs — no policy enum, no branch (MEL-ENGINE-IX, IV;
MEL-CODE-001). The `min`-deadline reduction is the shared mechanism every driver calls; what it does
with the ready set *is* the driver.

The driver holds four invariants. A source whose demand changed from another vat publishes the new
demand *before* it **rings a doorbell**, and the driver reads demands only *after* draining — so a
sleeping driver recomputes `min` without missing the change (a correctness ordering, not a footnote). A drain snapshots the source set and defers any open/close to
the turn boundary, so a source may mutate the set while it runs. Across the drain→block boundary it
loses no wakeup: it rechecks readiness atomically before sleeping (`MWMO_INPUTAVAILABLE` on win32;
level-triggered `epoll` is naturally safe; edge-triggered needs an explicit recheck), so input
arriving between the last drain and the wait cannot hang it. A coalescing **doorbell** is drained to
empty every wake — the mailbox is dequeued fully, so a surplus ring costs at most one harmless empty
wake; how many dequeued items are then *handled* per turn is the driver's affair above, a separate
concern from this lost-wakeup safety.

For a subordinate vat there is no driver: each source's wakeables are bridged to the OS loop and its
deadline emulated by the OS's timer/RAF.

    void mel_run (Mel_Vat*);
    bool mel_step(Mel_Vat*);

`mel_run` drives turns until no source or work retains the vat; `mel_step` runs one turn for a host
that owns the wait.

### Work — tasks, fibers, coroutines
Work is dispatched to a vat (affinity = that vat) or an executor, cancellable. It waits in one of three
ways — a stackful **fiber** suspend, a stackless **coroutine** suspend, or a **blocking** wait — all
one idea (wait for a completion) by different mechanisms. Blocking or heavy work is a **task** submitted
to an executor; a fiber or coroutine awaits it and suspends until it completes.

    Mel_Task   mel_spawn      (Mel_Vat*, Mel_Task_Fn,  void *arg);
    Mel_Task   mel_spawn_fiber(Mel_Vat*, Mel_Fiber_Fn, void *arg);
    Mel_Result mel_await      (Mel_Task);
    Mel_Result mel_wait       (Mel_Task);
    void       mel_cancel     (Mel_Task);

    Mel_Executor *mel_executor_open (Mel_Allocator*, isize n);
    Mel_Task      mel_submit        (Mel_Executor*, Mel_Task_Fn, void *arg);
    void          mel_executor_close(Mel_Executor*);

Three states, not two: a **vat sleeps** (its driver parked in the Waiter, zero CPU), **work suspends**
(a stackful fiber switch or a stackless coroutine return, either freeing the thread), and a **worker
thread blocks** (parked in a real syscall inside a task, consuming a whole thread). Suspend frees the
thread; block consumes one — which is why blocking is confined to executors, never a vat: a fiber or
coroutine on a subordinate vat yields to the OS loop exactly as on a sovereign one, and neither ever
blocks.
A completing task **rings the doorbell** of its awaiter's vat; that vat's driver — or, on a subordinate
vat, the OS bridge — drains the **mailbox** to resume the awaiter (one source multiplexes N tasks; a task
is never its own source). A vat stays live
while any source or outstanding awaited task retains it.

Awaiting is one idea — *suspend this work until a completion* — realized by a family of primitives,
because the mechanism differs while the idea does not: a **fiber** await (`mel_await`) suspends a
stackful frame; a **coroutine** await suspends a stackless frame (a state-machine return to the caller,
emitted by the transform, lexically scoped — no await in a callee); a **blocking** wait (`mel_wait`)
parks the calling thread. The mechanism *is* the primitive. The constraint is occupancy, not identity:
a vat's thread must *yield* — fiber or coroutine — and must never *block*, or it freezes the driver (the
OS loop on a subordinate vat; a debug assertion fires, MEL-ENGINE-VIII); a blocking wait is legal only
where parking a thread is the point — an executor worker. Across all three the result is the awaited
work's, so `Mel_Result` is a *carrier* (status-or-error + payload) and a typed binding
(`Mel_Device dev = …`) is an accessor over it, since C has no polymorphic return. The error half of
`Result` is owed.

### Application entry
    void mel_app_setup(Mel_Vat *root);

Opens sources, spawns work, opens further vats, and returns. Identical across platforms; the root
vat's sovereignty and the synthesized loop differ beneath it.

## Platforms

Each platform supplies the **Waiter** (sovereign), the **Bridge** (subordinate), the wakeable kinds,
and the root vat's sovereignty.

### linux
- Root: sovereign.
- Waiter: io_uring (`io_uring_enter`) when present, else epoll (`epoll_wait`).
- Wakeables: fds; timers as a timerfd or an io_uring timeout; an eventfd for cross-vat doorbells.
- Vblank: a DRM page-flip event fd, or the Wayland frame callback (an fd event).
- GUI: the Wayland/X11 fd is a source wakeable; the root stays sovereign — input is a source, not a
  subordination.

### macos / ios (darwin)
- Root: subordinate under AppKit/UIKit (CFRunLoop owns the main thread); sovereign for a headless
  CLI/server (kqueue).
- Waiter (sovereign): kqueue — `EVFILT_READ`/`WRITE`/`TIMER`/`MACHPORT`. No native completion; a
  reactor source performs the syscall on readiness, and regular-file work is offloaded to an executor
  (readiness cannot represent a file).
- Bridge (subordinate): wrap a wakeable in a `CFFileDescriptor`/`CFRunLoopSource` so CFRunLoop drains
  the source. The main thread stays AppKit's.
- Vblank: CVDisplayLink / CADisplayLink, bridged via a mach port; on a subordinate root it is a source
  the OS drives.

### win32
- Root: sovereign, but a windowed thread must pump messages.
- Waiter: headless — IOCP (`GetQueuedCompletionStatusEx`); windowed — `MsgWaitForMultipleObjectsEx`
  over event/waitable handles + `QS_ALLINPUT`, so overlapped I/O and the message queue share one wait.
- Wakeables: HANDLEs, waitable timers, the message queue; the DXGI waitable swapchain for vblank.
- The windowed driver drains messages each turn before draining other sources. A modal loop (resize,
  menu) hijacks the thread: a built-in source arms on `WM_ENTERSIZEMOVE`, installs an OS timer callback
  (a subordinate push) to keep the frame serviced, and disarms on `WM_EXITSIZEMOVE`. `mel_step`
  tolerates the re-entrancy.
- A windowed thread cannot wait IOCP and the message queue in one call, but it need not: IOCP is meant
  to be drained by a dedicated thread. A **bridge vat** owns the port (`GetQueuedCompletionStatusEx`,
  batched), enqueues results to the mailbox, and `SetEvent`s an auto-reset event — the **doorbell** —
  sitting in the windowed driver's `MsgWaitForMultipleObjectsEx` set (with `MWMO_INPUTAVAILABLE`).
  Idiomatic, not a workaround — the cross-vat doorbell instantiated.

### android
- Root: sovereign on the native app thread (ALooper, epoll); the Java UI thread is separate.
- Wakeables: looper fds; lifecycle and input arrive as looper-fd sources from Java.
- Vblank: AChoreographer native vsync, a poll-deadline source posted through the looper.

### wasm
- Root: **always subordinate** — no Waiter exists; the indefinite-block and spin points of the
  spectrum are unavailable, so a busy game degrades to RAF (MEL-ENGINE-VII).
- Bridge: emscripten event callbacks and `requestAnimationFrame` drain sources; `setTimeout` emulates
  deadlines; fetch/promise completions ring a doorbell into a source.
- `mel_app_setup` opens sources and returns; the browser drives.
- Fibers require Asyncify — no native stack switch — accepted for now.

## Use-cases

`mel_app_setup` is identical across platforms; the root vat's sovereignty and the synthesized loop
differ beneath it. Each case is a *declaration* — sources, work, vats — from which the idiomatic loop
emerges.

### native GUI, idle (win32 / macos)

    void mel_app_setup(Mel_Vat *root) {
        Mel_Window *w = mel_window_open(root, "title", 1280, 720);
        mel_window_on_event(w, on_event);
    }

The window registers one source: wakeable = the message queue / the AppKit source, deadline =
`MEL_NEVER`. The negotiation blocks until an event — `GetMessage` on win32, CFRunLoop on macos
(subordinate root). Zero CPU at rest (MEL-ENGINE-III, VI). No frame source exists, so nothing polls.

### game with dynamic focus (win32)

    void mel_app_setup(Mel_Vat *root) {
        mel_window_open(root, "game", 1920, 1080);
        g_frame = mel_frame_open(root, on_frame);
    }

The frame source's deadline is `0` in active gameplay (spin, max FPS — `PeekMessage`), `last+100ms`
when throttled (timed wait), and `MEL_NEVER` when minimized or paused (block — `GetMessage`). The
driver recomputes `min` each turn, so focus and lifecycle slide the loop along the spectrum with no
application code (MEL-ENGINE-VI).

### editor (event-driven UI, on-demand viewport)

A UI source at `MEL_NEVER` and a viewport-frame source at `0` while the user drags, `MEL_NEVER` when
idle. `min` spins the viewport during interaction and blocks the whole thread when it stops — both from
the same reduction, no mode flag.

### emscripten (any of the above)

The root is subordinate: the same sources become DOM listeners and RAF; `mel_app_setup` returns and
the browser drives. `on_event`/`on_frame` are unchanged; spin and indefinite-block do not exist here.

### server

    void mel_app_setup(Mel_Vat *root) {
        mel_listener_open(root, listen_fd, on_conn);
    }

One source: wakeable = the listen fd / IOCP, deadline = `MEL_NEVER`. The negotiation blocks in
`epoll_wait`/`GetQueuedCompletionStatusEx`. `on_conn` spawns each connection as work; a heavy handler
offloads to an executor.

### camera stream / devices (a module authoring a source)

    Mel_Stream s = mel_camera_open_stream(target, on_frame, user);

The camera module authors the source; per platform its vtbl chooses the discipline: an OS callback
registration (an inverted source on a capture vat), a poll-deadline source on `target`'s vat, or — for
permission — a one-shot proactor source awaited first. A platform lacking a capability emulates it
(MEL-ENGINE-VII). If the shape needs its own thread, the module opens a vat. The runtime's job is only
to make all three expressible through one source vtbl; the camera specifics are the module's. Gamepads
are the same: an inverted `WM_INPUT` source on one platform, an XInput poll-deadline source on another.
Audio is an inverted source on a dedicated vat.

### multithreaded, affinity-pinned startup

    static Mel_Executor *g_cpu;

    void mel_app_setup(Mel_Vat *root) {
        Mel_Vat *render = mel_vat_open(mel_vat_alloc(root), (Mel_VatDesc){ .pinned = true });
        g_cpu = mel_executor_open(mel_vat_alloc(root), 4);
        mel_spawn_fiber(root, boot, render);
    }

    void boot(Mel_Vat *render) {
        Mel_Device dev   = mel_await(mel_spawn(render, acquire_gpu, 0));
        Mel_Task   pipes = mel_spawn(render, build_pipelines, dev);
        mel_await(mel_read(asset_fd, buf, n));
        Mel_Task   decode = mel_submit(g_cpu, decode_assets, buf);
        mel_await(pipes);
        mel_await(decode);
    }

`acquire_gpu` and `build_pipelines` are pinned to the `render` vat. Asset bytes arrive by **async
I/O** — `mel_read` is awaited directly, parking no thread — and only the CPU-bound `decode_assets` is
a task on the `g_cpu` executor. Reaching for a blocking read on the executor here would be the user's
foot, not the framework's; the async path is the one given for free. `boot` is a fiber that suspends,
never blocks, across each await.

### a custom integration for many tiny jobs

A poor built-in job path is not rewritten from scratch: the application authors one source whose
wakeable is its own completion signal and multiplexes its jobs behind it, plugging into the same
negotiation (MEL-ENGINE-II, IV). One source, N jobs — never a source per job.

### cancellation

    Mel_Task t = mel_submit(g_cpu, decode, job);
    mel_cancel(t);

Cancellation is a **request, not a guarantee**: the OS may complete a cancelled op regardless. The
system resolves the awaiter as cancelled at once and still reaps the eventual completion — freeing its
buffer and OS registration — never assuming the op stopped. A source-backed op routes the cancel to
the OS (`CancelIoEx`, unregistering a callback) through the source vtbl's `cancel` entry; pure work
unwinds the await chain (MEL-ENGINE-VIII).
