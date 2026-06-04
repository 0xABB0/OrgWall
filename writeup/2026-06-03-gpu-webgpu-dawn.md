# 2026-06-03 — WebGPU backend (native, macOS) on vendored Dawn

Greenfield fourth GPU backend: `modules/gpu/src/webgpu/**`, built on the vendored
Dawn prebuilt (`third-party/webgpu`). Brings `gpu` + `hello-gpu` up under `--gpu=webgpu`.

## Work done

New backend dir `modules/gpu/src/webgpu/` (one internal header + per-area TUs), mirroring
the metal/vulkan/d3d12 shapes:

- `wgpu_backend.h` — instance/adapter/device/surface/swapchain/command-list/queue structs,
  the per-type resource slotmaps, and the shared helper prototypes.
- `common.c` — slotmap table ops, thread-tracker shims, submit-serial, format mapping
  (`Mel_Gpu_Format` ↔ `WGPUTextureFormat`), `WGPUStringView` helper, and the
  `wgpuInstanceProcessEvents` pump-tick poller.
- `instance.c` — `wgpuCreateInstance` + async `wgpuInstanceRequestAdapter`, drained at
  startup by pumping `ProcessEvents` (off-reactor `*_sync` pattern). One adapter exposed.
- `caps.c` — honest WebGPU-core tiers from `wgpuAdapterGetInfo`/`GetLimits`/`HasFeature`.
- `device.c` — async `wgpuAdapterRequestDevice` (drained), queue acquire, slotmaps, the
  completion pump with the `ProcessEvents` tick-source registered as a poller, device-lost
  + uncaptured-error callbacks.
- `resources.c` — buffers (`mappedAtCreation` upload, `MapRead` readback drained
  synchronously), textures, views, samplers; `texture_write`/`buffer_write` via
  `wgpuQueueWrite*`.
- `shader.c` — WGSL → `wgpuDeviceCreateShaderModule` via `WGPUShaderSourceWGSL`.
- `pipeline.c` — `wgpuDeviceCreateRenderPipeline` (vertex+fragment, vertex layout from the
  reflection-derived `Mel_Gpu_Vertex_Element[]`, color targets + blend, depth-stencil,
  multisample, topology/cull/front-face), auto pipeline layout.
- `surface.m` — CAMetalLayer on the cocoa NSView wrapped as a `WGPUSurface` via
  `WGPUSurfaceSourceMetalLayer`.
- `swapchain.c` — `wgpuSurfaceConfigure` (RenderAttachment, fifo/immediate by vsync).
- `record.c` — frame begin/end (acquire current texture → encoder → submit → present →
  ProcessEvents), render-pass begin/end, bind pipeline/vertex/index, draw/draw_indexed,
  copy texture→buffer and buffer→buffer; barriers are faithful no-ops (WebGPU auto-syncs at
  pass boundaries, spec P1); push-constants/dispatch are loud MissingFeature no-ops.
- `rendering.c` — offscreen `cmd_begin_rendering`/`cmd_end_rendering` (render-to-texture).
- `queue.c` — single implicit queue; `queue_submit` finishes encoders, submits, and routes
  completion to the reactor future via `wgpuQueueOnSubmittedWorkDone`, drained synchronously
  so the visual harness reads a resolved status immediately after submit.
- `misc.c` — `format_properties`, and the loud-MissingFeature surface for bindless slots,
  device address, sync primitives, query pools, bind-group layouts/groups.

`modules/gpu/build.c` (additive): `WHEN(.gpu="webgpu")` sources (`*.c` + macOS `*.m`),
`MEL_GPU_WEBGPU=1`, `mel_depends(lib, "webgpu")`, AppKit/QuartzCore/Foundation/Metal
frameworks for the `.m` surface, plus the new `gpu-webgpu` test target.

`test/test_webgpu.c` — caps-honesty test + WGSL triangle render-to-texture + readback with
programmatic pixel assertions + a test pinning the loud SPIR-V refusal.

## Dawn link approach + exact env

- **Link.** `gpu` unconditionally `mel_depends`-on `webgpu`. The third-party already gates
  its prebuilt fetch and PUBLIC `-lwebgpu_dawn`/`-L`/rpath under `WHEN(.gpu="webgpu")`, so
  on metal/vulkan/d3d12 variants the dep is inert (not fetched, contributes no flags).
  Verified: gpu + gpu-foundation still build and link under both `--gpu=metal` and
  `--gpu=vulkan`. No third-party wiring change was needed — `third-party/webgpu/build.c`
  was sufficient as-is.
- **Runtime env.** The Dawn dylib (`libwebgpu_dawn.dylib`) lives in the third-party prefix
  and is found via the rpath the build injects — no extra loader-path env is needed for it.
  The test runner still wants `DYLD_LIBRARY_PATH=/opt/homebrew/lib` (the repo-standard for
  the test harness) and `MEL_TEST_NOFORK=1`. Exact commands:
  - `./nob build hello-gpu macos --gpu=webgpu`
  - `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-webgpu macos --gpu=webgpu`

## Completion-pump wiring (spec §3.3)

The device's completion pump registers a poller that calls `wgpuInstanceProcessEvents` on
the WGPUInstance — the spec's "Pump on tick" source. Startup async ops (request-adapter,
request-device), buffer-map-for-readback, and queue-work-done are each drained by pumping
ProcessEvents in a bounded spin (the off-reactor `*_sync` pattern), since headless/test runs
have no reactor ticking the pump. Submit resolves the reactor future synchronously after the
drain so the visual harness sees an Ok status immediately.

## What renders vs owed

- **Renders.** WGSL triangle render-to-texture → readback, proven by `gpu-webgpu`
  (`webgpu_triangle_wgsl_readback`): center pixel is the three-color vertex blend, corners are
  the dark clear, ~722/4096 covered pixels (the triangle's screen area). The full path —
  instance → adapter → device → queue → buffer/texture/view → WGSL shader → render pipeline →
  begin-rendering → bind pipeline/vbo → draw → copy-to-buffer → map readback — is exercised.
- **Clear-and-present.** Swapchain configure + acquire/encoder/submit/present is implemented
  in `record.c`; device/surface/swapchain bring-up confirmed by launching hello-gpu (creates
  the instance + device on the Apple M3 Pro, honestly warns bindless stays capped).
- **Owed.** Pipeline cache, compute pipelines (need bind-group layouts), explicit bind
  groups / descriptor sets, push constants (not in WebGPU core), bindless slots, sync
  primitives, query pools, native interop/import, sampled-texture + uniform bind-group draws.
  All currently refuse loudly with MissingFeature, never silent no-ops.

## Honest caps tiers reported

`bindless = capped` (no true heap; sized-binding-array stepping-stone), `timeline = emulated`
(single implicit queue), `tile_local = emulated` (no subpass-input/PLS in core),
`ray_tracing = none`, `mesh_shaders = false`, `work_graphs = false`, video = none,
`residency = none`, `persistent_map = false`, `host_visible_device_local = full_uma` on the
integrated Apple adapter, `timestamp = quantized_100us` iff the feature is granted else none,
`bytecode_passthrough.wgsl = true`, `bytecode_passthrough.spirv = false` (see debt).

## Golden-diff deltas + tolerance

No golden diff was run, and **no golden file was created or modified**. Every committed
reference under `modules/gpu/test/golden/` is a bindless / compute / push-constant scene that
WebGPU core cannot reproduce, so none covers the WGSL triangle — there is no cross-backend
reference to diff against, and the existing macOS-Vulkan goldens are off-limits. Rendering is
instead proved programmatically (pixel assertions). The produced PPM is dumped to
`modules/gpu/build/macos-debug/webgpu_triangle_wgsl.ppm` as an inspection artifact. The
`MEL_GOLDEN` harness and the `"webgpu"` backend label remain wired-but-unused for a future
purpose-built webgpu reference. A justified cross-backend edge tolerance was drafted
(`max_channel_delta = 6`, `max_fraction_exceeding = 0.05`) for when such a reference exists.

## Build / test results

- `./nob build gpu macos --gpu=webgpu` — OK (18/18, all backend TUs + the `.m` surface).
- `./nob build hello-gpu macos --gpu=webgpu` — OK, packaged `.app`, links Dawn.
- `./nob build gpu macos --gpu=metal` / `--gpu=vulkan` — OK (webgpu dep inert).
- `gpu-foundation` under `--gpu=webgpu` — 13 passed / 0 failed.
- `gpu-webgpu` under `--gpu=webgpu` — 3 passed / 0 failed (caps honesty, WGSL triangle,
  loud SPIR-V refusal).
- `gpu-resources` under `--gpu=webgpu` — 0 tests (gated to vulkan/metal), 0 failed.

## Kludges (confess all, MEL-ENGINE-VIII)

1. **Synchronous drain of async ops.** request-adapter, request-device, buffer-map, and
   queue-work-done each spin-pump `wgpuInstanceProcessEvents` to completion with a bounded
   spin (100k iters × 100µs sleep) instead of riding the reactor pump. Justified for the
   off-reactor startup/tooling/headless path (the visual harness reads status right after
   submit and has no reactor running), but it is a busy-ish wait, and the bound is a magic
   number. The proper reactor-driven path exists (the pump poller is registered); these call
   sites just don't use it. Debt: route map/submit completion through the future + reactor
   when a reactor is present, and reserve the synchronous drain strictly for `*_sync`.
2. **Bounded scratch arrays.** `targets[8]`/`blends[8]`/`colors[8]` (pipeline/render-pass)
   and `buffers[8]`/`stack[8]` (submit) are fixed scratch with explicit overflow guards
   (8 = WebGPU max color attachments; submit overflows to a heap array). Not growable
   `[MEL_MAX_*]` state — mirrors the existing metal backend's `adapters[16]` — but still a
   fixed cap; flagged per MEL-CODE-002.
3. **`img_golden.c` linked but unused** in the `gpu-webgpu` target (no `MEL_GOLDEN` call this
   round). Harmless; kept so a future webgpu golden can wire in without a build edit.
4. **`buffer_mapped` never unmaps.** A readback buffer mapped via `buffer_mapped` stays
   mapped until destroy. Fine for one-shot readback in tests; a re-mappable lifecycle is owed.

## Open questions for Gabbo

1. **hello-gpu triangle does NOT render under webgpu, and I cannot fix it (file ownership).**
   Two compounding facts: (a) the vendored `eliemichel/dawn-prebuilt` **Release** build has
   Tint's SPIR-V reader compiled out — `WGPUShaderSourceSPIRV` is rejected at runtime with
   "SPIR-V is disallowed", and the `allow_unsafe_apis` toggle (instance- and device-level,
   both tried) does not re-enable it; (b) `apps/hello-gpu/src/triangle.c` hardcodes
   `MEL_GPU_SHADER_TARGET_SPIRV`. So under `--gpu=webgpu` the triangle scene fails **loudly**
   at `shader_create` (precise MissingFeature error → null pipeline → no draw, clear only) —
   honest, not a crash or silent no-op, but it does not render the triangle. To make the app
   render under webgpu the triangle (and the other scenes) must pass the WGSL bundle when
   `caps.shader.bytecode_passthrough.wgsl` is true — an app-side change outside my ownership.
   **Decision needed:** do you want the app to branch shader target on caps (app-owner task),
   or should we source a SPIR-V-reader-enabled Dawn prebuilt (third-party-owner task)? The
   render path itself is proven by `gpu-webgpu` rendering the identical WGSL triangle.
2. **Caps reports `spirv = false` for this prebuilt specifically.** If a future Dawn build
   ships the SPIR-V reader, that flag (and the `shader_create` SPIR-V refusal) should flip
   back. Worth a build-time probe rather than a hardcoded `false`?
3. **clear/blit/quad bundles are SPIR-V-only / bindless** and remain out of scope as briefed.
