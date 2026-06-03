# Slang shader frontend — target+blob bytecode API, slangc bundles, SPIR-V/MSL/WGSL

Matrix task #2 (U12, §6.4). Stand up the Slang single-source → per-backend bundle pipeline and
extend the bytecode shader API to carry a target + neutral blob, keeping Vulkan green.

## Work done

### API (foundational)
- `modules/gpu/include/gpu/shader.h`: `Mel_Gpu_Shader_Bytecode_Opt` gains `Mel_Gpu_Shader_Target target`
  and neutral `vertex_blob`/`fragment_blob` (+ sizes). Legacy `spirv_vertex`/`spirv_fragment` RETAINED
  as aliases (fallback when neutral blob unset) so every existing call site — apps and the other lanes'
  tests (`modules/gpu/test/*`) — compiles unchanged. Compute analog: `compute_blob` + `spirv` fallback.
  New `Mel_Gpu_Shader_Target` enum {SPIRV=0, MSL, DXIL, WGSL}; SPIRV=0 so a zero-init opt = legacy
  behavior. New status `MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED`.
- `modules/gpu/src/vulkan/shader.c`: both create fns resolve neutral-blob-else-spirv, and loud-fail
  (`mel_log_error` + TARGET_UNSUPPORTED) on any non-SPIRV target (MEL-ENGINE-VIII).
- `modules/gpu/include/gpu/caps.h`: new `Mel_Gpu_Caps_Shader_Bytecode_Passthrough {spirv,msl,dxil,wgsl}`,
  added as `bytecode_passthrough` field on `Mel_Gpu_Caps_Shader`.
- `modules/gpu/src/vulkan/caps.c`: `out->shader.bytecode_passthrough.spirv = true;`.

### Vendoring + generation
- Slang is already vendored via `third-party/slang/build.c` (a prior lane): per-host `mel_prebuilt`
  fetch of the upstream `v2026.10.2` zip into a gitignored `build/<plat>/prefix/` yielding the `slangc`
  CLI + `libslang`, plus a C wrapper `mel_slang_compile`. I reused that rather than re-vendoring.
  Populate the prefix with `./nob build slang-compile <platform>`.
- `tools/build/vendor/slang/SLANG_VERSION.lock`: records the pin (version, upstream tag, per-host URL
  pattern, pin-source = `third-party/slang/build.c`). `.gitignore` (dist/) already present.
- `apps/hello-gpu/shaders/gen_bundles.sh`: committed generator. Drives the vendored `slangc` to emit,
  per `.slang`, per-entry SPIR-V (`-target spirv -profile spirv_1_5 -fvk-use-entrypoint-name`) + MSL
  (`-target metal`) + WGSL (`-target wgsl`) + reflection JSON, and writes committed `*_bundle.h`. SPIR-V
  is mandatory; MSL and WGSL are best-effort per shader with a loud WARN and a `*_HAS_MSL`/`*_HAS_WGSL`
  guard in the header when a target can't be expressed.

### Shaders authored (representative set) + bundles generated
- `apps/hello-gpu/shaders/slang/{triangle,blit,gradient,quad,clear}.slang` (Slang/HLSL rewrites of the
  GLSL originals).
- Generated `apps/hello-gpu/src/{triangle,blit,gradient,quad,clear}_bundle.h`.
  - triangle/gradient/quad: SPIR-V + MSL + WGSL all emitted.
  - blit/clear: SPIR-V only — Slang's plain-HLSL `NonUniformResourceIndex` over an UNBOUNDED resource
    array is rejected by both the Metal and WGSL targets (needs the `melody.binding` mixin, which §6.4
    lists as out-of-scope here). SPIR-V (Vulkan) is correct and complete.

### Wiring (P1 proof)
- `triangle.c` and `bindless_present.c` now consume the Slang SPIR-V bundles via the new `target`+blob
  API and the bundle's recorded entry names. Both render under `--gpu=vulkan`.

## Validation
- gpu-vulkan: **48 passed / 0 failed** (baseline 48 — unchanged). gpu-visual: **11/11** (baseline 11).
  slang-compile: **3/3**.
- hello-gpu builds clean; all 19 app call sites compile unchanged through the aliased API.
- hello-gpu `HELLO_GPU_AUTO=triangle` run under the Khronos validation layer: zero ERROR/WARN, zero
  validation callbacks → Slang SPIR-V loads, reflects (vertex loc0 RGB32F / loc1 RGBA32F, stride 28),
  pipelines, and draws correctly. plasma/raymarch (bindless present via Slang blit) also silent.
- MSL validated with `xcrun -sdk macosx metal -c`: triangle/gradient/quad vs+fs all compile to AIR.
- The generated blit SPIR-V was disassembled and confirmed to carry `OpTypeRuntimeArray` at
  set0/binding0,1 + a `Root` push-constant struct (offsets 0,4) — identical binding layout to the
  legacy GLSL `blit_spv.h`, so `uses_bindless_set` + push-constant size reflect identically.

## Kludges / debt (confessed)
- **caps.c touched outside the enumerated ownership list.** Deliverable #2 ("Vulkan reports spirv=true")
  forces a one-line write in `modules/gpu/src/vulkan/caps.c`, which my file-ownership list did not name.
  I added exactly `out->shader.bytecode_passthrough.spirv = true;` and nothing else. Flagging it.
- **MSL/WGSL unverified for bindless shaders.** blit/clear ship SPIR-V only; their Metal/WGSL forms are
  genuinely not emittable from naive Slang. Debt: the `melody.binding` mixin (§6.4) is the real fix.
- **WGSL unverified by a real frontend.** No tint/naga/dawn on this host; WGSL arrays are syntax-checked
  by inspection only. Committed for the future WebGPU agent but unproven.
- **Bundle reflection JSON is generated but not yet consumed.** The header carries entry names + Slang
  version; the engine still reflects from SPIR-V at load (existing `mel_gpu__spirv_reflect`). Consuming
  Slang's reflection JSON (D4) is owed (P3).
- **Legacy `*_spv.h` left in place** for the 8 un-migrated screens (`gallery, postprocess, bloom, layers,
  reacdiff, passthrough, raymarch, msaa` include `blit_spv.h`; `passthrough` also `triangle_spv.h`). No
  symbol clash (separate TUs). Migration is owed work (P4).
- **Design D2 contradicted the no-codegen-pass constraint.** The spec said "DECIDED: real codegen pass";
  CLAUDE.md forbids registering an undocumented codegen pass. I did NOT register one — used a committed
  script — and rewrote D2 to record the divergence and surface the codegen-pass decision to Gabbo.

## Owed shaders (GLSL → Slang, not yet rewritten)
blit_spv.h-class screens aside, the remaining GLSL sources to port: `blit.frag` variants in the 8
screens above, plus `bloom_*` (5), `boids_draw/boids_sim`, `buildargs`, `cells`, `cull`, `depth_only`,
`fullscreen` (covered indirectly), `instances`, `mandelbrot`, `msaa_compose`, `particle_draw/sim`,
`plasma`, `post`, `raymarch`, `reacdiff_*` (3), `scene3d`, `shade`, `shadow_*` (4), `star`,
`scene3d`. ~32 of 37 GLSL shaders remain. The API is complete; only shader CONTENT is phased.

## Pre-existing defect found (not mine)
`mandelbrot_spv.h` (hand-compiled, untouched) declares SPIR-V `DrawParameters` without the matching
`shaderDrawParameters` feature → one validation ERROR on its pipeline create. Reproduces independent of
this lane. Worth a fix in the mandelbrot screen's own SPIR-V or a feature enable.

## CLAUDE.md suggestions (recommendations only)
- Document the codegen-pass registration surface, or explicitly bless the committed-artifact +
  generator-script pattern for shader bundles, so future lanes don't each rediscover the halt-and-query.
- Note that `third-party/slang` (CLI + libslang, per-host prebuilt) is the canonical Slang vendoring;
  `tools/build/vendor/slang` is only the version lock.

## Suggestions
- Promote `gen_bundles.sh` to a manifest-driven generator over a committed shader list once more screens
  migrate; keep it script-only until Gabbo rules on the codegen pass.
- Land the `melody.binding` mixin early — it unblocks MSL/WGSL for every bindless screen at once.
- Have the bundle header embed the reflection JSON (or a compacted form) so the Slang-version key and
  binding shape travel with the blob for the §6.5 pipeline-binary cache fingerprint.
