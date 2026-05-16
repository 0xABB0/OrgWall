# Frame Timing and Game Loop Integration

## Problem Statement

The library has no notion of a per-frame tick. The current code runs in a strictly event-driven mode: a widget redraws only when something invalidates it; there is no concept of "fire a callback every vsync." This rules out games entirely, rules out smooth animation, rules out anything that needs to run code at the display's refresh rate.

I want games and forms in the same library without forcing forms apps to pay for game-style ticking. Forms should remain event-driven and idle when nothing changes; games should opt in to vsync-driven frame callbacks and own their per-frame logic; mixed apps (a level editor with a 3D preview pane and surrounding native UI) should support both modes simultaneously, with frame ticks flowing only to the widgets that asked for them.

I also need per-frame transient memory (string formatting, draw command lists, AABB scratch) without pressuring the heap allocator. Game frames produce garbage; that garbage is uniformly resettable at frame boundaries.

## Solution

Per-widget opt-in frame ticks. A widget calls `mel_gui_request_frames(handle, true)` and from then on receives `on_frame(handle, dt_ns, user)` callbacks at the platform's vsync cadence. Widgets that do not request frames receive nothing and pay nothing.

Frame cadence is driven by per-platform vsync sources: `CADisplayLink` on Cocoa/iOS/visionOS, `Choreographer` on Android, blocking `Present` plus a high-resolution timer or `DwmFlush` on Win32, `requestAnimationFrame` on web, OpenXR frame loop on XR platforms. The chosen source delivers a tick to the reactor that owns the relevant widgets; the reactor's frame dispatcher then calls each opted-in widget's `on_frame`.

Frame ticks fire in this order each iteration:

1. Reactor drains its posted-work ring (any cross-thread messages that arrived since the last iteration)
2. Reactor drains its platform message pump (any native events queued)
3. Frame dispatcher calls `advance_frame` on every input module that has registered for it (kbm, touch, gamepad, xr — see PRD 09)
4. Frame allocator is reset
5. Frame dispatcher calls `on_frame` on every widget on this reactor that opted in

This ordering guarantees that during `on_frame`, input state polling sees correctly-rolled edge bits, and the frame allocator is empty and ready.

A `Mel_Frame_Allocator` is provided as an arena allocator that resets automatically at the start of each frame on its reactor. Users allocate transient frame data from it; nothing needs to be freed. Allocator instance per reactor.

## Implementation Decisions

- A widget opts into frame ticks via `mel_gui_request_frames(handle, bool enabled)`. Default off.
- The capability callback `on_frame(handle, i64 dt_ns, void* user)` lives in a new capability struct, `Mel_Gui_Frame_Cb`, embedded in any widget's opt struct that supports it (Viewport always; Canvas optionally; Stage optionally; others by request).
- Per-platform vsync sources are owned by the reactor's platform backend. The reactor module exposes "register a vsync subscriber" so the GUI's frame dispatcher subscribes once per reactor.
- The frame dispatcher iterates only over widgets that opted in. The opt-in list is per-reactor; widgets remove themselves on destroy or when calling `request_frames(false)`.
- Frame `dt_ns` is the elapsed time since the previous frame on the same reactor, as a signed 64-bit nanosecond delta. The first frame after opt-in delivers dt_ns based on the previous vsync tick if available, or zero.
- A `Mel_Frame_Allocator` is provided in a new module `allocator.frame`. It is an arena: bump-allocator with a reset op. The reactor's frame dispatcher resets the per-reactor frame allocator immediately before calling `on_frame` callbacks. Users access it via `mel_alloc_frame()` (returns the current reactor's frame allocator) or by passing the reactor explicitly.
- The frame allocator has a configurable capacity per reactor (default ~1 MB) and asserts on overflow in debug builds; in release, overflow falls through to heap with a one-time log warning.
- Render-thread separation: if a viewport's `on_frame` posts work to a different reactor that owns the swapchain, that other reactor is responsible for its own vsync ticking. The library does not enforce a render-thread model; consumers wire up what they want using reactor/post primitives.
- Forms-only apps that never call `mel_gui_request_frames` see zero frame dispatch overhead. The cost of the frame dispatcher is paid only when at least one widget on the reactor has opted in.

## Testing Decisions

A good test of the frame system verifies: opting in causes `on_frame` to fire at approximately platform vsync (within a tolerance, since vsync is not perfectly deterministic in test environments); opting out stops firing; `dt_ns` reflects actual elapsed time between calls; the frame allocator is empty at the start of each `on_frame`; allocations on it within a frame succeed and are valid until the next frame.

Modules under test: the reactor's vsync integration, the frame dispatcher in `gui/`, `allocator.frame`.

Prior art: `mel_old/gpu.*` had Vulkan present-loop tests that drove vsync-based frame timing. The pattern (drive frames in a tight loop, count, assert rate) is reusable.

## Out of Scope

- A retained-mode animation system (interpolators, easing curves, timeline). Useful, but its own scope.
- Fixed-timestep simulation decoupled from variable-rate rendering (the classic game-physics pattern). Apps implement this themselves on top of `on_frame`.
- Render-thread machinery (separate reactor pulling double-buffered command lists from the game-thread reactor). The primitives support it; the assembly is application code.
- Frame budget enforcement (warning when `on_frame` takes too long). Useful for profiling; not load-bearing.
- VRR / variable refresh rate handling. The library passes through whatever the platform delivers in `dt_ns`; consumers handle variable rates.

## Further Notes

The frame-tick model is the only "games support" affordance the framework provides at this layer. Everything else (GPU, swapchain management, command buffer assembly, render-thread sync) is the consumer's responsibility, built on top of the locked primitives: reactors for threading, viewports for surfaces, jobs for sim parallelism, channels for cross-thread data, frame allocator for transient memory.

Forms apps remain entirely event-driven by default. The framework does not idle-tick anything; if no widget opts into frames and no events arrive, the reactor sleeps.

Per-reactor frame allocator means apps with multiple reactors (multi-window desktop, separate render thread) get separate arenas and can clear them on their own cadences.
