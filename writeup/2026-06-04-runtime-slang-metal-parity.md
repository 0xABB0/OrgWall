# Runtime Slang + Metal feature parity — session recap & next steps

State at stop: `origin/main` = `5999d3cd`, in sync. Desktop (macOS) runtime-Slang fully working
across Vulkan/Metal/WebGPU. Stopped mid-gallery-port by request.

## Goal
Replace committed per-backend shader bundles with ONE Slang source per effect, compiled at runtime
(C23 `#embed` of the `.slang`), and reach feature parity so every hello-gpu screen renders on every
backend — chiefly Metal, which previously drew nothing for bloom/boids/SDF/mandelbrot.
Decisions taken this session: runtime Slang everywhere; full parity scope; dual-lane bindless
(keep §6.7 device-global heap on Vulkan/D3D12/WGSL, Metal diverges to Slang's per-dispatch
argument buffer); android/iOS libslang built from source; redistribute the win32 DXC payload.
Design: `design/gpu-runtime-slang.md` + `design/gpu-slang-bindless.md`.

## Work done (all merged to main)
- **Wrapper** (`third-party/slang`): `mel_slang_compile` → {SPIRV, MSL, DXIL(win32-gated), WGSL} +
  `mel_slang_compile_reflect` (vertex attrs, bindings, push-constant size, compute `[numthreads]`)
  + a per-target macro injection (`MEL_TARGET_METAL/_SPIRV/_WGSL/_DXIL`).
- **Runtime gpu API** (`modules/gpu/src/slang.c`): `mel_gpu_shader_create_from_slang` /
  `mel_gpu_pipeline_create_from_slang` (+ compute variant with reflected threadgroup). Target chosen
  from `caps.shader.bytecode_passthrough`; reflection-driven vertex layout. **Sync only** (async +
  cache deferred — see next steps). Loud diagnostics on compile failure.
- **Metal parity**: compute encoder (`cmd_dispatch`/indirect + `MTLComputeCommandEncoder`),
  argument-buffer bindless heap (slot==index + `MTLResidencySet`), **dual-lane Slang bindless** on the
  compute encoder (#37) and the render encoder (#38) — `DescriptorHandle<T>` lowers to a per-dispatch
  argument buffer the RHI builds from resolved slots. Fixed two no-op Metal stubs found en route:
  `mel_gpu_texture_write` and device-local `buffer` upload (staging blit).
- **Vendoring**: wasm libslang **verified** (slang-wasm-libs archive runs under node, emits WGSL);
  win32 DXIL emit+sign **verified on win-pilot**; android/iOS `mel_unavailable` (recipe verified, see
  residuals). Desktop unchanged.
- **Shader port**: 18 of the gallery's ~22 screens ported to single-source dual-lane Slang and proven
  via the backend-agnostic `gpu-scene` golden-diff suite. **gpu-scene: vulkan 18/18 · metal 16/18 ·
  webgpu 6/18.** All four originally-missing screens (bloom, boids, raymarched-SDF, mandelbrot) render
  on Metal **bit-identical (delta 0)** to the Vulkan oracle via runtime Slang.

## NEXT STEPS (priority order)

1. **Finish the gallery port (4 screens left).** `instances`, `gallery`, `layers`, `postprocess`
   still include `*_spv.h`. Port each to `apps/hello-gpu/shaders/slang/<screen>.slang` following the
   established pattern (exemplars: `bloom.slang`, `mandelbrot.slang`, `bindless_present.slang`):
   single-source multi-entry; `#embed` + `from_slang` in the screen `.c`; dual-lane bindless via the
   `#if defined(MEL_TARGET_METAL)` split (Metal `DescriptorHandle<T>` Root fields; Vulkan/WGSL keep
   `[[vk::binding(N,0)]]` heap arrays + uint slots); ONE unified Root per screen (the Metal RHI binds
   the arg buffer at fixed `[[buffer(0)]]`). Add each as a `gpu-scene` scene + a macOS-Vulkan-oracle
   golden in `modules/gpu/test/golden/shared/`. `--embed-dir=apps/hello-gpu` is already set.

2. **Metal `dispatch_indirect` (#39) — one-line lift.** `mel_gpu_cmd_dispatch_indirect`
   (`modules/gpu/src/metal/macos/record.m:~594`) does NOT build the from-slang bindless per-dispatch
   argument buffer that `mel_gpu_cmd_dispatch` builds (`record.m:~584-589`); an indirectly-dispatched
   bindless shader binds no resources on Metal. Lift the same arg-buffer build into it, then remove
   the `dispatch_indirect` Metal `MEL_SKIP` in `test_scene.c`. (Vulkan oracle already proves the algo.)

3. **Metal MSAA resolve.** `mel_gpu_cmd_begin_rendering` (`modules/gpu/src/metal/macos/rendering.m`)
   ignores `Mel_Gpu_Color_Attachment.resolve_view` — no `resolveTexture` / `MTLStoreActionMultisample-
   Resolve`. Wire it, then remove `msaa`'s Metal `MEL_SKIP`. This also un-stubs `msaa.c`'s on-screen
   resolve (a silent no-op on Metal today). After 2+3, Metal gpu-scene reaches 18/18.

4. **test_scene.c comment-strip.** The port agents left `/* … */` banner + explanatory comments
   (against the no-comments rule, MEL-CODE). Strip them. (The take-both merge that broke main was
   already repaired by a deterministic G1∪G2 reconstruction; this is the residual cleanup.)

5. **Runtime-Slang completeness (deferred from the desktop-first de-risk):**
   - **async + cache (#32 deferred I3/I5).** Every `*_from_slang` currently recompiles synchronously,
     so a screen hitches on first show. Add the §3.3 job-bridged completion future + the
     content-addressed persistent blob cache (sha256 of slang-version‖source‖entry‖stage‖target‖
     defines) + compile-ahead-at-load, per `design/gpu-runtime-slang.md` I3/I5/I10.
   - **Vendoring residuals (#36).** android/iOS libslang: **DECISION NEEDED** — the in-graph
     source-build you chose needs a NEW two-phase build pass (host generators → NDK/iOS cross) which
     is halt-and-query; a cross-build-once-and-mirror recipe is verified (android `libslang-compiler.so`
     26 MB stripped). Until resolved, `slang` is `mel_unavailable` on android/iOS and those gpu builds
     loud-fail (gpu depends on slang unconditionally since #32). **wasm**: slang-wasm is single-threaded
     and cannot link into hello-gpu's `--shared-memory` module → needs the §3.3 separate-Web-Worker
     compile path (gpu-module work); also a pre-existing emscripten-5.0.7 break in
     `modules/app/src/web/lifecycle_web.c` (`em_beforeunload_callback` now returns `const char*`).
     **win32 DXC payload**: redistributing `dxcompiler.dll`+`dxil.dll` needs a `mel_prebuilt` tar
     fallback (unzip absent on win-pilot) + a co-location hook in `modules/build/thirdparty.c`.

## Open conventions / forks (in the per-task writeups; decide when convenient)
- Metal per-dispatch arg-buffer (16–40 B) pooling (deferred; #37/#38).
- Unified-Root-per-screen couples logically-independent passes on Metal (forced by fixed
  `[[buffer(0)]]`). Alternative: teach the RHI the reflected per-entry arg-buffer index, or a way to
  mark entries non-bindless (G2 debt).
- `DescriptorHandle<ConstantBuffer<T>>` rejected by Slang 2026.10.2 → uniform-buffer bindless stays
  on the classic path (accept?).
- WebGPU core has no device-global bindless heap → 12/18 gpu-scene scenes honestly skip on webgpu;
  revisit when `GPUResourceTable` ships.
- `gpu/format.{h,c}` is F32X2/3/4 only; grow when a screen needs int/uint/packed vertex attributes
  (none did this session).
- `dispatch_indirect` shade de-raced to a deterministic serial splat for the golden — accept as
  canonical, or keep the racy parallel scatter + loosen the golden?
- Old residuals still open: #13 (build-system: backend-keyed gpu obj dir + no silent-default test
  backend; `mel_depends_when` already done) and #16 (linux runtime: Docker run, loader-stub real
  loader, XCB text/keyboard).

## Kludges / debt (confessed — bar is zero)
- The take-both merge malformed `test_scene.c` and duplicated the file-level `#if/#else/#endif` guard;
  repaired by deterministic reconstruction (ours base+G1 ∪ G2 scene block + 5 embed defs +
  `#include <gpu/format_props.h>`). main green again.
- Legitimate out-of-fence fixes by port agents: `mel_gpu_texture_write` (Metal), device-local buffer
  upload (Metal), the SV_VertexID reflection fix (slang wrapper). All kept.
- The four items in NEXT STEPS 1–4 are the known remaining gaps to full macOS parity.

## CLAUDE.md suggestions (recommendations only — not applied)
- Document the mandatory macOS GPU-suite env: `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1`.
- Note the runtime-Slang single-source authoring (dual-lane `#if MEL_TARGET_METAL`) + `#embed` +
  `--embed-dir=apps/hello-gpu` in the gpu / hello-gpu readme.
