# 2026-06-03 — WebGPU backend on wasm (browser lane)

Brings `modules/gpu/src/webgpu/**` up under `wasm --gpu=webgpu`, the honest browser-WebGPU
lane (design/gpu-rhi.md §2 / §3.3). The web runtime is the **browser's** WebGPU via
Emscripten's `emdawnwebgpu` port — single-threaded, async-only — not Dawn-native. The native
backend's synchronous spin-pump cannot run on web (control must return to the JS event loop);
that path is replaced with an ASYNCIFY drain.

## Key reality discovered

Emscripten 5.0.7 ships **emdawnwebgpu** (Dawn's `webgpu.h` for Emscripten), exposing the same
modern unified `<webgpu/webgpu.h>` surface the native Dawn-7187 backend already targets
(`WGPUStringView`, `WGPUCallbackMode_AllowProcessEvents`, `wgpuInstanceProcessEvents`,
`wgpuInstanceCreateSurface`). So the backend C compiles for wasm almost unchanged — the port,
not the legacy `-sUSE_WEBGPU=1` html5 binding, is the path. Enabled with
`--use-port=emdawnwebgpu` at **both compile and link**.

## What was platform-gated, and the emscripten equivalents

1. **Canvas surface (replaces `surface.m`).** New `src/webgpu/wasm/surface.c` creates the
   `WGPUSurface` from an HTML-canvas CSS selector via
   `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector`. The DOM GUI backend
   (`modules/gui/src/dom/gpu_view.c`) already hands `mel_gpu_surface_create` a `const char*`
   selector (`"#mel-gpu-<N>"`) where macOS hands an `NSView*`; the wasm surface consumes the
   selector. `surface.m` stays gated `MEL_ON(MACOS)` (byte-identical, untouched body).

2. **Synchronous spin-pump (the spin-pump confession from the native writeup).** The four
   off-reactor `*_sync` drains — request-adapter (`instance.c`), request-device (`device.c`),
   buffer-map (`resources.c`), queue-work-done (`queue.c`) — each spun
   `while(!done){ wgpuInstanceProcessEvents(); mel_thread_sleep(100µs); }`. On the
   single-threaded browser this deadlocks (the promise only settles when control returns to JS;
   `mel_thread_sleep` blocks the only thread) **and** fails to link (`mel_thread_sleep` has no
   wasm backend in `modules/thread`). Consolidated the four duplicated loops into one helper
   `mel_gpu__drain_until(instance, &done)` in `common.c` (MEL-ENGINE-IX), platform-gated inside:
   - native: `wgpuInstanceProcessEvents` + `mel_thread_sleep(100000)` — **byte-equivalent** to
     the old loop (same 100µs, same 100k bound).
   - wasm: `wgpuInstanceProcessEvents` + `emscripten_sleep(1)` under `-sASYNCIFY` —
     `emscripten_sleep` unwinds to the JS event loop, the WebGPU promise settles, then rewinds.
     The off-reactor `*_sync` drain made honest on web (spec §3.3), keeping the synchronous API
     shape intact (MEL-ENGINE-II: simple path = powerful path).

3. **`WGPUQueueWorkDoneCallback` signature divergence.** The one genuine API difference between
   native Dawn-7187 and emdawnwebgpu: native is
   `(status, userdata1, userdata2)`; emdawnwebgpu inserts a `WGPUStringView message` parameter:
   `(status, message, userdata1, userdata2)`. `mel_gpu__work_done_cb` in `queue.c` is now
   `#ifdef __EMSCRIPTEN__`-gated to match each header. (Adapter / device / map / uncaptured-error
   / device-lost callbacks are identical across both headers — no gate needed.)

## Stacktrace stub (cross-module link blocker, FLAGGED)

`modules/debug` had no wasm backend for `mel__platform_stacktrace_capture` (undefined symbol
blocked the wasm link — first flagged in `writeup/2026-06-03-gpu-platform-matrix-round4.md`). Added
`modules/debug/src/wasm/stacktrace.c`: a loud honest stub — clears the trace to zero frames,
writes `"stacktrace: capture unavailable on wasm ..."` to stderr, returns `false` (no silent
default; MEL-ENGINE-VIII). The consumer (`assert.c`) tolerates zero frames safely
(`mel_stacktrace_format` returns empty on `frame_count == 0`; `mel_stacktrace_free` handles NULL
frames). Gated `WHEN(.platforms = MEL_ON(WASM))` in `modules/debug/build.c`. The source guard uses
`MEL_PLATFORM_EMSCRIPTEN` (the C-source macro for wasm; `MEL_PLATFORM_WASM` is the build-system
enum, not a C define).

## gmtime_r (blocker #2 — already resolved)

The `__EMSCRIPTEN__` guard in `modules/log/src/log.sink.sqlite.c` (`gmtime` into a stack `tm`)
already landed on `origin/main`; the wasm compile no longer hits it. No residual one-line libc gap
remained in the gpu/debug/log link path.

## Third-party link gate (FLAGGED, one-line, blocking the wasm link)

`third-party/webgpu/build.c` injected `-lwebgpu_dawn` for **all** `WHEN(.gpu="webgpu")` with no
platform restriction. On wasm that breaks the link (emdawnwebgpu provides webgpu via the port, not
`-lwebgpu_dawn`). Gated that link line to `MEL_ON(MACOS) | MEL_ON(ANDROID)` — the platforms whose
prebuilt/cmake actually produce the dylib. `mel_depends(gpu, "webgpu")` stays; on wasm the
third-party target now contributes no flags (inert), exactly as it is inert on metal/vulkan.

## Build wiring (`modules/gpu/build.c`, additive)

```
mel_sources(lib, WHEN(.gpu="webgpu", .platforms=MEL_ON(WASM)), "src/webgpu/wasm/*.c");
mel_cflags (lib, MEL_PUBLIC, WHEN(.gpu="webgpu", .platforms=MEL_ON(WASM)), "--use-port=emdawnwebgpu");
mel_link   (lib, MEL_PUBLIC, WHEN(.gpu="webgpu", .platforms=MEL_ON(WASM)),
            "--use-port=emdawnwebgpu", "-sASYNCIFY", "-sALLOW_MEMORY_GROWTH=1");
```

The `*.c` glob (`src/webgpu/*.c`) is non-recursive, so the wasm surface lives in
`src/webgpu/wasm/` and is selected explicitly. PUBLIC cflag/link propagate to the hello-gpu app
TUs + final link (the emdawn JS glue is pulled in at link). The build framework's existing web-GUI
path emits `.html` + `--shell-file modules/build/web/shell.html` when `gui` is in the closure.

## Commands + results

- `./nob build gpu macos --gpu=webgpu` → **OK** (18/18; shared-file edits compile native).
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-webgpu macos --gpu=webgpu`
  → **3 passed / 0 failed / 0 skipped** (caps honesty, WGSL triangle render+readback, loud SPIR-V
  refusal). Native regression intact after every gate.
- `./nob build hello-gpu wasm --gpu=webgpu` → **GREEN**, clean rebuild from scratch (49/49).
  Artifacts: `apps/hello-gpu/build/wasm-debug/hello-gpu.{html (526B), js (130KB), wasm (256KB)}`.
  The `.js` carries 198 WebGPU/wgpu glue references (emdawn JS bundled). No errors, no undefined
  symbols at link.
- **Serve.** `cd apps/hello-gpu/build/wasm-debug && python3 modules/build/web/serve.py . 8723`
  then open `http://localhost:8723/hello-gpu.html`. Verified HTTP 200 for html/js/wasm.
  (`./nob run hello-gpu wasm` invokes serve.py with only the dir and no port — serve.py wants
  `dir port`; pass a port explicitly until the driver is fixed — build-framework, out of lane.)

## Browser run status — HONEST DEFERRAL (not faked)

No headless WebGPU browser is available in this environment: no Chrome/Chromium/Edge/Brave is
installed (only Safari, no scriptable headless WebGPU path here), no puppeteer cache, and `deno`'s
built-in WebGPU is server-side (no DOM, cannot load an Emscripten browser page). Per the task I did
**not** fake a render.

What a WebGPU-capable browser **would** show today, traced honestly through the code:

- **Blocking gap (out of lane): no web app-runtime entry.** `main` + `mel_app_setup` live in
  `modules/app/src/posix/app.c`, gated `MEL_ON(MACOS) | MEL_ON(LINUX)`. The `app` module has **no
  `src/web` / wasm entry**, so on wasm nothing calls `mel_app_setup` → `gpu_host_init` /
  `gpu_host_open` never run → emcc dead-strips the GUI/GPU host (confirmed: `mel_gpu_render_source_new`,
  `gpu_host_open` absent from the final wasm). The page loads but the host does not bring up a
  device or canvas. Authoring `modules/app/src/web/*.c` (an `emscripten`-driven entry that calls
  `mel_app_setup` on the reactor) is the app-runtime owner's task, outside this matrix-#7 lane
  (webgpu backend / debug stub / hello-gpu packaging-shell).
- **Once that entry exists**, the backend path is ready: instance → adapter → device resolve via
  the ASYNCIFY drain on the browser event loop; the canvas surface binds `#mel-gpu-<N>`; the
  swapchain configures; `frame_begin`/`cmd_begin_pass`(clear)/`frame_end` would **clear-and-present**
  each rAF tick (the reactor's web backend already drives frames via
  `emscripten_request_animation_frame`). The **triangle scene specifically would not render** — same
  as native macOS webgpu — because `apps/hello-gpu/src/triangle.c` hardcodes
  `MEL_GPU_SHADER_TARGET_SPIRV` and Tint's SPIR-V reader is off, so `shader_create` refuses loudly
  → clear-only (app-side caps-branch task; native writeup open-question #1).

## Native macOS WebGPU regression

`gpu-webgpu macos --gpu=webgpu`: **3/3 pass** after all platform-gating. The drain helper's native
branch is byte-equivalent to the prior loops; the `#ifdef __EMSCRIPTEN__` queue-callback gate is
inert on native.

## Shared-file edits (FLAGGED)

- `modules/gpu/src/webgpu/common.c` — added `mel_gpu__drain_until` (platform-split inside); native
  branch byte-equivalent to the old per-site loops.
- `modules/gpu/src/webgpu/{instance,device,resources,queue}.c` — each spin loop replaced by one
  `mel_gpu__drain_until` call; `queue.c` also gates the work-done callback signature on
  `__EMSCRIPTEN__`. `instance.c` dropped its now-redundant direct `<thread/thread.h>` include.
- `modules/gpu/src/webgpu/wgpu_backend.h` — declared `mel_gpu__drain_until`.
- `modules/gpu/build.c` — additive wasm sources + `--use-port=emdawnwebgpu` (cflag+link) +
  `-sASYNCIFY` + `-sALLOW_MEMORY_GROWTH=1`.
- **Cross-module:** `modules/debug/build.c` + `modules/debug/src/wasm/stacktrace.c` (new wasm
  stacktrace stub).
- **Third-party:** `third-party/webgpu/build.c` — one-line platform-gate on the `-lwebgpu_dawn`
  link (exclude wasm).

`surface.m` and all other backend bodies are untouched on the native path.

## Kludges (confess all, MEL-ENGINE-VIII)

1. **ASYNCIFY drain, not reactor-routed.** On wasm the four `*_sync` drains still busy-spin
   `ProcessEvents` + `emscripten_sleep(1)` to completion (bounded 100k iters) rather than riding
   the reactor pump. This is the native writeup's kludge #1 carried to web: it works (ASYNCIFY
   yields each iter so promises settle) and keeps the synchronous API honest, but it is a yield-spin
   with a magic bound, and `-sASYNCIFY` adds code-size/perf overhead to **every** call stack.
   Debt: route map/submit/device-create completion through the future + reactor when a reactor is
   present, reserving the drain for true off-reactor `*_sync`. The pump poller is already registered
   (`device.c`); these call sites just don't use it yet.
2. **`-sASYNCIFY` is whole-program.** Applied globally (default), not scoped to the WebGPU async
   functions via `ASYNCIFY_ONLY`. Heavier than necessary; a future pass can list the exact yielding
   functions. Flagged.
3. **`mel_gpu__drain_until` bound is still a magic 100000.** Inherited from the native loops;
   unchanged. No reactor-cap rationale, just a large ceiling.
4. **Surface width/height = 0 on wasm.** The canvas surface does not query the DOM element size; the
   swapchain takes its extent from the explicit `width/height` the resize callback passes. Fine for
   the current flow (the host always passes the canvas size), but the surface itself is size-blind —
   a `mel_gpu_surface_reconfigure` only records the values, it cannot resize a canvas it never
   measured.

## Open questions for Gabbo

1. **Web app-runtime entry.** hello-gpu cannot actually run on the browser until `modules/app` has
   a web entry that calls `mel_app_setup` on the reactor (emscripten-driven, live-runtime). That is
   the app-runtime owner's file, not mine. Do you want me (or the app-module owner) to author
   `modules/app/src/web/*.c`? It is the single gating piece between "links + serves" and "renders in
   a browser."
2. **Headless browser for CI run-proof.** No Chromium-family browser exists on this host. Provision
   one (or a puppeteer/playwright Chromium with `--enable-unsafe-webgpu`) if you want an automated
   per-pass browser render capture; Safari has no scriptable headless WebGPU path here.
3. **Triangle SPIR-V on web.** Same as the native writeup's open-question #1: the hello-gpu scenes
   hardcode SPIR-V; on web (Tint SPIR-V reader off) they refuse loudly and clear-only. Branch the
   app's shader target on `caps.shader.bytecode_passthrough.wgsl` (app-owner), or source a
   SPIR-V-reader-enabled build. Unchanged by this pass.
4. **`./nob run hello-gpu wasm` serve port.** The driver passes only the dir to `serve.py`, which
   also wants a port (`int(sys.argv[2])`) — it would crash. A one-line driver fix (default port) is
   out of my lane; flagging it.
