# GPU Support: a multi-backend GPU abstraction (Metal, WebGPU, Vulkan)

This documents the work that gave Melody a GPU rendering abstraction with a
clean, backend-agnostic public API and four concrete backends, plus the GUI
seam that lets GPU surfaces live inside ordinary native windows beside regular
widgets, the build-system plumbing for selecting a backend, and the example app.

The shape of the public API is modeled on `rafx` (flat, opaque-handle,
descriptor-driven, begin/record/end-frame); the dispatch mechanism is modeled
on the GUI module (compile-time backend selection, one backend's sources
compiled per build, no vtables).

## What was delivered

- A `modules/gpu/` library: device, swapchain, buffer, shader, pipeline,
  command list / frame flow, and a reactor render source. Opaque handles whose
  concrete structs are defined per backend.
- **Four backends**, all rendering the same `hello-triangle`:
  - **Metal** (`src/metal/`) — default on macOS/iOS. Verified on macOS.
  - **WebGPU** (`src/webgpu/`) — default on web, via the Emscripten
    `emdawnwebgpu` port. Verified in a browser.
  - **Vulkan** (`src/vulkan/`) — default on Linux/Android; selectable on macOS
    over MoltenVK. Verified on macOS (MoltenVK) and on a physical Android phone.
  - DX12 is **not** implemented (see "What remains").
- A **GPU backend axis** in the build system, independent of the GUI backend
  axis, with per-platform defaults and a `--gpu <backend>` CLI override.
- A **GUI seam**: a `mel_gpu_view` component (a layer/surface-hosting view, not
  a window) implemented for the cocoa, dom, and androidnative GUI backends. It
  exposes a native surface handle; the GPU module attaches its swapchain to it.
  gui and gpu have no compile dependency on each other — the app glues them.
- The **render loop** is a reactor source (the "GPU vsync waitable" the reactor
  README describes), today a 60 Hz timer, swappable for native vsync later.
- `apps/hello-gpu/`: a native GUI window with buttons; each button opens a new
  top-level window hosting a "graphical app" (an `init`/`render`/`teardown`
  interface) rendered into a `mel_gpu_view` beside a native label. The triangle
  is the first such app.

## Public API shape (`modules/gpu/include/gpu/`)

Opaque handles (`Mel_Gpu_Device`, `_Swapchain`, `_Buffer`, `_Shader`,
`_Pipeline`, `_Command_List`), `_Opt` structs + `{__VA_ARGS__}` creator macros
exactly like the GUI controls. Frame flow:

```c
mel_gpu_frame_begin(sc);
Mel_Gpu_Command_List* cmd = mel_gpu_frame_commands(sc);
mel_gpu_cmd_begin_pass(cmd, clear);
mel_gpu_cmd_bind_pipeline(cmd, pipe);
mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
mel_gpu_cmd_draw(cmd, 3, 1);
mel_gpu_cmd_end_pass(cmd);
mel_gpu_frame_end(sc);
```

`mel_gpu_device_create` returns a handle (not a forced singleton); a backend
restricted to one device would return the same one. The swapchain takes a
`native_window` (`void*`) that each backend pair interprets (NSView* on cocoa,
canvas selector string on web, ANativeWindow* on Android).

## The backend axis (`tools/build/`)

The GUI backend axis (`cocoa`/`winui`/`dom`/…) is a single global value per
build and is owned by the GUI module, so the GPU backend needed its own axis.
It is a clone of the existing backend-axis plumbing:

- `mel_build_use_gpu_backend_on(t, platform, name)` + a default table:
  `metal`→apple, `dx12`→win32, `vulkan`→linux/android, `webgpu`→web
  (`runner_platform.c`).
- Source discovery additively pulls in `src/<gpu_backend>/` for every module,
  exactly like `src/<backend>/` (`runner_discovery.c`). Only the gpu module has
  those subdirs.
- `--gpu <backend>` CLI flag (`runner_driver.c`), with per-platform validation.
  The build variant line now reads e.g. `macos/cocoa/metal/native`.

### Flag gating

Two new gating dimensions were added to build properties (`Prop` gained
`runtime` and `gpu_backend` fields; `prop_resolve` checks them):

- **runtime-gated** (`mel_build_add_cflag/link_flag_on_runtime`): used for the
  emscripten-only `--use-port=emdawnwebgpu` and `--pre-js`, so they never reach
  the wasi-sdk clang.
- **gpu-gated** (`mel_build_add_cflag/link_flag_on_gpu`): used for `-lvulkan`,
  which is only linked when the vulkan backend is selected.

## The GUI seam (`mel_gpu_view`)

A GUI component, never a window. Per GUI backend:

- **cocoa** (`gpu_view.m`): a layer-hosting `NSView`. `mel_gpu_view_surface`
  returns the `NSView*`; the Metal/Vulkan swapchain attaches a `CAMetalLayer`.
- **dom** (`gpu_view.c`): a `<canvas>` with a stable `#mel-gpu-N` id;
  `mel_gpu_view_surface` returns the CSS selector string.
- **androidnative** (`gpu_view.c` + `MelGpu`/`MelGpuView` Java): a `SurfaceView`;
  `mel_gpu_view_surface` returns the `ANativeWindow*`.

Resize/lifecycle flows through one `on_resize` callback: cocoa fires it from
`setFrameSize:`, web from `set_bounds`, Android from `surfaceChanged`/
`surfaceDestroyed`. The host (`gpu_host.c`) uses it as a unified seam: create the
swapchain on the first resize that has a ready surface, resize on later ones,
tear down on a zero size (Android's surface loss). macOS/web have a ready
surface at layout time; Android follows the async `SurfaceView` callbacks.

## How to build and run

    ./nob run hello-gpu macos                 # Metal
    ./nob run hello-gpu macos --gpu vulkan     # Vulkan via MoltenVK
    ./nob run hello-gpu web                    # WebGPU (emdawnwebgpu) in the browser
    ./nob run hello-gpu android                # Vulkan on device (default gpu axis)

Click "Open hello-triangle" to spawn the GPU window.

**Toolchain prerequisites (this session's machine, Apple Silicon):**
- macOS Vulkan: Homebrew `vulkan-loader` + `molten-vk` (loader at
  `/opt/homebrew/lib`, ICD auto-found at `/opt/homebrew/etc/vulkan/icd.d`).
- Web WebGPU: emcc 5.x with the `emdawnwebgpu` port (fetched automatically by
  `--use-port=emdawnwebgpu`). Requires a WebGPU-capable browser.
- Android: NDK with API ≥ 24 (`libvulkan.so` first ships at android-24).

## Shaders

Slang was **deferred**. Each backend is fed its own shader source for the
triangle, all carried in one `Mel_Gpu_Shader_Opt`:
- Metal: MSL string, compiled at runtime (`newLibraryWithSource`).
- WebGPU: WGSL string, compiled at runtime (`wgpuDeviceCreateShaderModule`).
- Vulkan: SPIR-V, authored in HLSL and compiled with `glslc` to `vs_main`/
  `fs_main` entry points with explicit `[[vk::location]]`, embedded as a C array
  in `apps/hello-gpu/src/triangle_spv.h`.

## Verification

- macOS / Metal — triangle rendered, screenshotted.
- Web / WebGPU — triangle rendered in a browser (confirmed by the user).
- macOS / Vulkan (MoltenVK) — triangle rendered, screenshotted.
- Android / Vulkan — triangle rendered on a physical device, captured via
  `adb exec-out screencap`. Native label + GPU surface share the window.

## Hacks and shortcuts taken this session

These are the deliberate compromises made to get four backends working in one
session. Each is a candidate for follow-up cleanup.

1. **Hardcoded Homebrew paths for macOS Vulkan.** `modules/build.c` adds
   `-I/opt/homebrew/include` / `-L/opt/homebrew/lib`. This is Apple-Silicon
   Homebrew only — no `$VULKAN_SDK` detection, no Intel `/usr/local`, no
   `pkg-config`. Should become a proper third-party/SDK-discovery step.
2. **Precompiled SPIR-V checked into the tree.** `triangle_spv.h` is generated
   by hand with `glslc` and committed; regeneration is manual, not wired into
   the build. A build-time shader compile (or Slang) should replace it.
3. **Triple-authored shaders.** The triangle's shader exists three times (MSL,
   WGSL, HLSL→SPIR-V). This is the cost of deferring Slang.
4. **Fixed window geometry in `gpu_host.c`.** Frames opened directly (not via
   the screen system) are not auto-sized, so the host hardcodes
   `mel_gui_set_bounds(frame, 60, 60, 640, 480)`. The `60,60` offset and size
   are magic numbers chosen for the demo. (It also papers over a web-backend
   issue: an unsized `.mel-frame` div is 0×0 and `overflow:hidden` clips it.)
5. **Vulkan single-frame-in-flight.** One command buffer + one set of
   semaphores + one fence; `mel_gpu_frame_begin` waits the fence every frame. No
   pipelining. Buffer staging uses `vkQueueWaitIdle`.
6. **Vulkan assumes graphics queue == present queue.** The device is created
   before any surface exists; `vkGetPhysicalDeviceSurfaceSupportKHR` is queried
   at swapchain time but its result is ignored. True on MoltenVK and the tested
   Android device; not general.
7. **`opt.debug` is a no-op.** No validation/debug layers are enabled on any
   backend, even when `debug=true` is passed.
8. **`mel_gpu_buffer_map` returns NULL** on the webgpu and vulkan backends — no
   persistent mapping or dynamic buffer update; only the initial `.data` upload
   at creation is supported.
9. **Web surface plumbing.** `mel_gpu_view_surface` returns a pointer into a
   `static` buffer (fine only because the swapchain copies the selector
   immediately). The swapchain is first created at a possibly-stale size and
   corrected by the next `on_resize`.
10. **Android gralloc noise.** During `SurfaceView` buffer negotiation, logcat
    shows non-fatal `gralloc5 ... unsupported format` / `vkGetSwapchainGralloc
    Usage2ANDROID failed: -11` lines before the swapchain settles on a supported
    format. They are harmless but ugly.
11. **Android surface teardown is happy-path only.** `surfaceDestroyed` tears
    the swapchain down (size-0 `on_resize`), but backgrounding/rotation/
    re-creation cycles were not exercised on device.
12. **`ANDROID_API` bumped 23 → 24 globally** (`runner_android.c`), and the
    default `MIN_SDK` to 24 (`runner_stages.c`), because `libvulkan.so` first
    ships in the NDK at API 24. This raises the floor for *all* Android apps, not
    just GPU ones.
13. **Web device acquisition via a JS pre-init.** WebGPU device creation is
    async; to keep `mel_gpu_device_create` synchronous, a `--pre-js`
    (`device_preinit.js`) acquires the adapter/device behind an
    `addRunDependency` gate, and C fetches it with
    `emscripten_webgpu_get_device()`. Works, but couples the app to that pre-js.

## What remains to be done

- **DX12 backend** (Windows) + a `mel_gpu_view` in the `winui` GUI backend.
- **Native WebGPU via Dawn** on desktop. Designed but not built: vendor Dawn as
  a third-party target producing `libwebgpu_dawn`, gate the existing webgpu
  backend (`#if defined(__EMSCRIPTEN__)` → port + canvas surface + implicit
  present; `#else` → Dawn headers + Metal-layer surface + `wgpuSurfacePresent`),
  add `webgpu` to macOS's valid gpu backends. The present call is already
  guarded for this.
- **Slang** runtime shader compilation, to replace the per-backend sources.
- **Vulkan gaps**: Linux window-system surface (xcb/xlib/wayland; only Apple and
  Android surfaces exist), real multi-frame-in-flight, present-queue selection,
  robust Android surface lifecycle, validation layers via `opt.debug`.
- **iOS**: the Metal backend should work, but no `uikit` `mel_gpu_view` was
  written, so it is untested.
- **Resource breadth**: only a vertex+color triangle path exists. No textures,
  samplers, uniform/index-buffer binding, depth, or MSAA — the enums for index/
  uniform usage exist but nothing binds them.
- **Buffer update / mapping** API.
- Replace the hardcoded Homebrew Vulkan paths with proper SDK discovery.

## Suggested CLAUDE.md updates

Add a "GPU" section to the repo `CLAUDE.md`. Proposed text:

> ## GPU rendering
>
> The `gpu` module is a backend-agnostic GPU abstraction (device, swapchain,
> buffer, shader, pipeline, command/frame, render source). The backend is chosen
> by an independent **gpu axis**, separate from the GUI backend:
>
>     ./nob build <target> <platform> --gpu <metal|vulkan|dx12|webgpu>
>
> Per-platform defaults: `metal` (macOS/iOS), `dx12` (win32), `vulkan`
> (linux/android), `webgpu` (web). Override per target in `build.c` with
> `mel_build_use_gpu_backend_on(t, platform, name)`. Backend sources live in
> `modules/gpu/src/<backend>/`; only the selected one compiles.
>
> Implemented: Metal, WebGPU (Emscripten `emdawnwebgpu` port), Vulkan
> (Linux/Android natively, macOS via MoltenVK). DX12 is not implemented.
>
> A GPU surface is a GUI component, `mel_gpu_view` (not a window): it hosts a
> swapchain inside an ordinary frame beside native widgets. `mel_gpu_view_surface`
> returns the platform handle the swapchain attaches to. The render loop is a
> reactor source (`mel_gpu_render_source`).
>
> Example: `apps/hello-gpu` — `./nob run hello-gpu <platform>`.
>
> Toolchain notes: macOS Vulkan needs Homebrew `vulkan-loader` + `molten-vk`;
> web needs emcc with the emdawnwebgpu port and a WebGPU browser; Android needs
> NDK API ≥ 24 (the native floor was raised to 24 for the Vulkan loader).
> Shaders are currently authored per backend (MSL / WGSL / SPIR-V-from-HLSL)
> until Slang is integrated.
>
> Build-property gating helpers added for this: `mel_build_add_{cflag,link_flag}
> _on_runtime` (e.g. emscripten-only) and `..._on_gpu` (e.g. vulkan-only).

## File inventory

New:

    modules/gpu/include/gpu/{gpu,types,device,swapchain,buffer,shader,pipeline,command,render_source}.h
    modules/gpu/src/render_source.c
    modules/gpu/src/metal/{metal.h,device.m,swapchain.m,buffer.m,shader.m,pipeline.m,command.m}
    modules/gpu/src/webgpu/{webgpu_backend.h,device.c,swapchain.c,buffer.c,shader.c,pipeline.c,command.c,device_preinit.js}
    modules/gpu/src/vulkan/{vulkan_backend.h,device.c,swapchain.c,buffer.c,shader.c,pipeline.c,command.c,surface.m,android_surface.c}
    modules/gui/include/gui/controls/gpu_view.h
    modules/gui/src/cocoa/gpu_view.m
    modules/gui/src/dom/gpu_view.c
    modules/gui/src/androidnative/gpu_view.c
    modules/gui/src/androidnative/java/orgwall/melody/platform/{MelGpu,MelGpuView}.java
    apps/hello-gpu/build.c
    apps/hello-gpu/src/{main.c,gpu_host.h,gpu_host.c,graphical_app.h,triangle.h,triangle.c,triangle_spv.h}

Modified:

    tools/build/{build.h,build.c,internal.h,runner_platform.c,runner_discovery.c,runner_driver.c,runner_graph.c,runner_android.c,runner_stages.c}
    modules/build.c
    modules/gui/include/gui/gui.h
    modules/gui/src/cocoa/macos.h
    modules/gui/src/dom/{web.h,backend.c}
    modules/gui/src/androidnative/{android.h,backend.c}
