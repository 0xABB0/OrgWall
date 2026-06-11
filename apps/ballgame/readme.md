# ballgame

A pure GPU game on the framework: one ball, moved with WASD/arrows (drag steers it
on touch platforms). The whole scene renders through the gpu module — a single
alpha-blended pipeline drawing CPU-built vertices (background gradient, fading
trail, glow, ball, specular highlight as SDF circle sprites) into a swapchain on
a `mel_gpu_view` inside a regular GUI window, paced by a 60 Hz
`mel_gpu_render_source`. Exercises boot entry, gpu device/swapchain/pipeline,
per-frame vertex upload, and keyboard + pointer input on every platform the
gpu module builds on (macos, win32, linux, wasm). ios/android are blocked
tree-wide for every gpu app: gpu depends on slang, which has no upstream
prebuilt for those platforms (`third-party/slang/readme.md`).

The data path deliberately sits on the lowest backend floor — plain vertex
buffers, no push constants, no bindless — so the same code runs on WebGPU core.

Shaders: `shaders/slang/ballgame.slang`, precompiled into
`src/ballgame_bundle.h` (SPIR-V + MSL + WGSL) by `shaders/gen_bundles.sh`;
`src/bundle_select.h` picks the first form the device's bytecode-passthrough
caps accept. DXIL is not minted yet (needs the Windows DXC pass; win32's default
backend is Vulkan, which takes the SPIR-V lane).

Deps: boot, vat, allocator, core, gui, gpu, log, string.
