# Reactor and Threading Model

## Problem Statement

I need a concurrency story that works coherently across every platform I want to target — desktop with arbitrary thread counts, mobile with strict main-thread rules, XR with platform-managed compositor threads, web with single-threaded mainmost and worker isolation, and wearables with constrained resources. Today the GUI layer is strictly single-threaded with `mel_gui_assert_ui_thread()` calls everywhere, the threading concept is hardcoded as "the UI thread," and there is no shared primitive for "execute on this specific execution context." Different platforms have different rules about what "main thread" even means, and the code currently jails the architecture into a model that breaks on platforms without a single fixed main thread.

I also want this concurrency layer to be useful outside the GUI — for headless tools, for the audio path, for game simulation, for whatever future runtime I attach to it. The GUI library should consume the concurrency primitives, not own them.

## Solution

Replace the "UI thread" concept with a first-class execution-context primitive — a reactor — that any OS-thread-bound work loop maps to cleanly. A reactor drains a posted-work queue, drives a platform message pump if one is attached, fires timers, and dispatches I/O readiness events. One reactor binds to exactly one OS thread for its lifetime.

On every platform there is at least one **system reactor**, handed to me at startup by the framework. On platforms that allow multi-threaded UI (Windows, Linux X11/Wayland), I can create additional **user reactors** that spawn new threads with their own loops. On platforms with strict single-thread UI (macOS, iOS, Android, web, watchOS, Vision OS), `mel_reactor_create()` returns NULL with a clear error; only the system reactor exists.

Cross-thread dispatch is uniform: `mel_reactor_post(target_reactor, fn, user)` enqueues work for a reactor to run on its thread. From inside the target reactor's thread, posting may inline. From outside, it goes through a lock-free MPSC ring drained at the top of the reactor's loop iteration. The same primitive serves "execute on UI thread" on web/mobile (no-op-ish when already on it), "marshal to render thread" on desktop, "send a result back from a worker" on every platform.

Sim-parallel work runs on a **worker pool** — N OS threads serving non-affine compute jobs. Built on the existing fiber-based `async.job` system. Workers are not reactors and reactors are not workers. The default worker pool exists per process; additional named pools can be spawned for logical isolation (audio DSP separate from game sim, for example).

The GUI layer becomes one consumer of reactors: a `Mel_Gui_Handle` is affine to a specific reactor at creation time, and all operations on it must be performed on that reactor's thread. The library replaces every assertion of "I am on the UI thread" with "I am on the reactor that owns this handle."

## Implementation Decisions

- A new module `async.reactor` exposes the reactor primitive. Public type `Mel_Reactor`, opaque. Public functions for create, destroy, current, run, stop, post, assert-on.
- The system reactor is created by the platform-specific bootstrap layer before user code runs and is retrievable via a well-known accessor.
- User reactors are spawned by `mel_reactor_create()`. The function is allowed to fail on single-threaded-UI platforms; failure returns NULL.
- Posted work uses a fixed-capacity lock-free MPSC ring per reactor. No heap allocation on the post path; capacity is a build-time constant per reactor.
- Reactors integrate with the platform's native message pump (Win32 `GetMessage`/`PeekMessage`, Cocoa `NSRunLoop`/`NSEvent`, Android `Looper`, X11/Wayland event fds, web `requestAnimationFrame` plus `MessageChannel`). The integration is in `async.reactor`'s per-platform backend, not in `gui.platform.*`.
- Worker pools live in `async.job` (existing module, kept). Pool creation supports a name and a thread count. The default pool is implicit and unnamed.
- Render-thread separation for games is not enforced. The framework supports it via the same primitives: a viewport that wants a render thread creates a user reactor on platforms that allow it, posts frame-snapshot work to that reactor, and lets that reactor own the swapchain. The library has no opinion; it just provides reactors.
- GUI handles are affine to one reactor. Cross-reactor access fails an assertion in debug builds and is undefined in release.
- The thread-local "UI thread id" cached state in the current `gui.c` is removed. Replaced with per-handle reactor pointer; assertion becomes `mel_reactor_assert_on(handle_owner_reactor)`.

## Testing Decisions

A good test of the reactor primitive exercises only its observable behavior: that posted work runs in the order it was posted, on the right thread, that timers fire at requested times, that drainage on iteration boundary is complete, that cross-thread posts visibly arrive at the next iteration of the target reactor, and that `assert_on` panics from the wrong thread. Internal queue capacities, ring head/tail indices, and pump integration paths are implementation detail and not tested.

The modules under test are `async.reactor` (in isolation), `async.job` (worker pool semantics), and the integration between them when posting completes from a worker back to a reactor.

Prior art for these tests is sparse in the current repo. The `mel_old/test.harness.c` provides a pattern for in-process harnesses; the new tests would follow that shape — driver threads creating reactors, scheduling work, asserting observed sequencing — but as new tests, not extensions of the old harness.

## Out of Scope

- The GPU swapchain and render-thread machinery itself. That belongs in the `gpu` module when it is rebuilt; this PRD only ensures the concurrency primitives can support it.
- Channels, signals, counters as primitives in their own right — see the module taxonomy PRD.
- Reactor scheduling fairness across multiple user reactors. Each reactor is its own thread; the OS scheduler handles it.
- Asyncify-style stack rewriting on web. Web reactors use the browser's event loop natively; long-running C work that wants to yield uses generators or jobs, not Asyncify.

## Further Notes

The name "reactor" is taken from the POSA1 pattern (Schmidt, 1996) — it describes exactly what this primitive does. The alternatives (`Event_Loop`, `Run_Loop`, `Looper`) are equivalent in meaning; "reactor" is locked because it is single-word, archaic enough to be unambiguous to readers who know the term, and not borrowed from a specific platform's API.

"Main thread" as a phrase is banned from the cross-platform API. Internal documentation may reference platforms' native main-thread names where doing so clarifies the platform implementation.
