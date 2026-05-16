# Web Platform Support

## Problem Statement

The web is a target unlike the others. The DOM is strictly main-thread. Workers exist but cannot touch the DOM. Real shared memory between main and workers requires `SharedArrayBuffer` and Atomics, which in turn requires COOP/COEP HTTP headers — your hosting setup has to cooperate. There is no `mmap(PROT_EXEC)`, so any runtime codegen approach is out. The browser owns the event loop, not the app. All I/O is Promise-based; from WASM, blocking I/O requires Asyncify, which roughly doubles WASM size and adds runtime overhead. The toolchain is either Emscripten (POSIX-ish, Asyncify, JS interop, threading via Workers) or wasi-sdk (cleaner standards story, needs more polyfills for DOM and threading).

Apps targeting the web range from "tiny demo, single-threaded, smallest possible WASM" to "full game with worker-based audio and physics, COOP/COEP enabled, real concurrency." The framework should support both without forcing either decision on apps that do not need it.

The framework should not pretend the web is just another desktop platform. It should accept the platform's constraints honestly and let apps configure how to live within them.

## Solution

The web is a first-class platform but with configurable mode. Defaults are aimed at smallest viable web app:

- Single-threaded reactor. `mel_reactor_create()` always fails on web; only the system reactor exists.
- Asyncify off. Blocking I/O must be avoided; use generators or callback-based async.
- Toolchain: Emscripten.
- Backend: `dom` — controls are real DOM elements (`<button>`, `<input>`, `<canvas>` etc.), accessible to screen readers, browser-styled.
- Worker pool: serialized to the main reactor (work submitted to the worker pool runs synchronously on the main thread; the API stays the same, the concurrency is gone).

Apps that need more turn flags on in `build.c`:

- Enabling web threading: spins up real Workers backed by SharedArrayBuffer, requires the deployment to set COOP/COEP headers. `mel_reactor_create()` still fails (workers are not reactors with DOM access), but the worker pool runs actual parallel jobs.
- Enabling Asyncify: roughly doubles WASM size but allows blocking C code to yield when calling into JS-backed I/O.
- Switching toolchain to wasi-sdk: smaller binaries for compute-heavy apps that do not need DOM, polyfills needed for any platform features.

All locked primitives (Mel_Reactor, Mel_Gui_Handle, _opt patterns, capability callbacks, input modules, build targets) survive untouched. The web platform implementation lives in per-backend / per-platform files like every other platform.

## Implementation Decisions

- A new platform identifier `MEL_PLATFORM_WEB` is added.
- Two web backends are supported initially: `dom` (DOM widgets, default) and `canvas` (everything drawn to a single canvas via WebGPU or WebGL, for game targets).
- The build system has web-specific flags settable from `build.c`:
  - `mel_build_web_threading(t, bool)` — enables COOP/COEP-requiring threading
  - `mel_build_web_asyncify(t, bool)` — enables Asyncify
  - `mel_build_web_toolchain(t, "emscripten" | "wasi-sdk")` — toolchain choice

- The system reactor on web is implemented by integrating with the browser event loop via `requestAnimationFrame` (for frame ticks), `MessageChannel` (for inter-task posting), and DOM event listeners (for input).
- The DOM backend creates real DOM elements for each widget. A button is a `<button>`. A label is a `<span>`. An edit is an `<input type="text">`. A canvas is a `<canvas>` with a 2D context. A viewport is a `<canvas>` with a WebGPU or WebGL context.
- DOM event listeners on widget elements dispatch into the appropriate input modules (kbm, touch) and into the widget's capability callbacks.
- The `gui.web/` tier-2 module (analogous to `gui.desktop`) exposes web-specific affordances: DOM access, history API, service worker registration, IndexedDB bridge, share API. Apps that include it commit to web targeting.
- Asset loading on web uses `fetch()`. The framework provides a wrapper that returns a future (signal-based completion); apps await it via signal or convert to a generator.
- Workers, when enabled, are pthread-backed via Emscripten. The same `async.job` worker pool API is used; under the hood Emscripten translates `pthread_create` to `new Worker`. SharedArrayBuffer carries shared memory.
- When threading is off, the worker pool degrades to serial execution on the main thread. Job submission still returns; the work runs synchronously inside the submission call (or queued to the next reactor iteration, depending on the worker pool's implementation). API contract is preserved; concurrency is the only thing missing.
- WASM binary is placed at `build/<target>/web/<config>/app.wasm` plus the loader JS, HTML shell, and asset bundle. Hosting integration (writing COOP/COEP headers if threading is on, gzip/brotli encoding, cache-busting) is the deploy concern; the build produces ready-to-host output.

## Testing Decisions

A good test of the web platform verifies that a minimal Melody app builds to a runnable WASM bundle, that a simple click on a DOM button fires the expected callback, that frame ticks via `requestAnimationFrame` deliver `on_frame` at approximately 60 Hz, that flipping the threading flag produces a build that uses Workers when COOP/COEP are set.

Modules under test: the web reactor backend, the web DOM widget implementations, the web build pipeline.

Prior art: none in the repo. Web testing infrastructure (headless browser, Playwright or equivalent) is part of the implementation work.

## Out of Scope

- Server-side rendering of Melody apps. The framework targets the browser.
- WebAssembly Components (WIT) integration. Too early; if/when stable, future PRD.
- WebXR for the same project as the XR module — when a Melody app wants XR on web, that is `xr/` (PRD 09) with a WebXR backend, separate from the main web backend.
- Progressive Web App manifest generation, service worker scaffolding. Useful additions, but lateral to the core web support.
- Hot-reload of WASM in development. The web build outputs a static bundle; reload-on-source-change is a dev-server concern.

## Further Notes

The "everything is configurable, everything has sane defaults" principle from the build system PRD applies directly here. Web apps that want simplicity get a small, single-threaded, no-Asyncify build. Web apps that need power flip toggles and pay the cost.

The DOM backend is locked as the default for `gui.control` widgets on web because it is the only path that gives accessibility for free, integrates with browser features (autofill, password managers, IME), and is what users of a web app expect. Apps that want fully custom-rendered UIs use viewport with WebGPU/WebGL and do their own rendering, but that is opt-in, not default.

Cross-thread `mel_reactor_post` to the main reactor from a worker works regardless of whether the worker is a real OS thread or a sibling task on the main thread — the posted-work ring is the same primitive in both modes; the only difference is whether the post is observably concurrent.
