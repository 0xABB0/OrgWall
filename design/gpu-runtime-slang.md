# Runtime Slang shader pipeline

Design spec. Supersedes `gpu-slang-shaders.md`'s offline-bundle model: **one `.slang` source per effect, compiled at runtime on every platform** (libslang linked everywhere — macos/win32/linux/android/ios/wasm). The app `#embed`s the `.slang` source as a NUL-terminated string and compiles it at load/runtime to the device's native target via `caps.shader.bytecode_passthrough`. Drives feature parity: every hello-gpu screen renders on every backend.

Implements `gpu-rhi.md` §6.4 (shaders), §6.5 (pipelines), §3.3 (async/reactor), §3.4 (caps). Honors MEL-ENGINE-I (no capability omitted — accept the runtime cost on mobile/web), -VI (binary size + battery budgeted), -VIII (loud diagnostics, no silent fallback).

## Decisions (fixed by Gabbo)

- Runtime libslang on **every** platform. No offline-bundle lane. The committed `*_spv.h` / `*_bundle.h` artifacts are retired.
- Authoring is **one `.slang` per effect**, multi-entry, embedded via C23 `#embed`.
- The screen calls `mel_gpu_shader_create_from_slang(...)`; the engine compiles to the device's native target and feeds the existing `shader_create_from_bytecode` lane.
- Compile is async (§3.3 future, job-bridged) with a persistent on-disk cache and compile-ahead-at-load.

## 1. Per-platform libslang vendoring

Research findings ground every decision here.

**Upstream availability.** shader-slang GitHub releases ship `slangc` + shared library + headers for **{windows, linux, macos} × {x86_64, aarch64}** only. No android, no ios, no wasm prebuilt. Library renamed (≥ v2025.21): `libslang.so`/`slang.dll` → `libslang-compiler.so`/`slang-compiler.dll`; old-name symlinks present in releases until end-2026, removed thereafter. ABI is **unstable** (major version pinned 0); link against the fully-versioned filename. The pin stays in `third-party/slang/build.c` (`SLANG_VERSION`) + `tools/build/vendor/slang/SLANG_VERSION.lock`.

**android (arm64), ios (arm64).** No prebuilt. Build libslang from source cross-compiled:
- android: NDK + `cmake --preset android-arm64`; generators built host-first (`-DSLANG_GENERATORS_PATH=...`) because Slang runs build-time codegen.
- ios: no upstream preset; bespoke toolchain file (arm64-apple-ios), generators host-first, same generator path. Coordinate the toolchain delta; emits MSL → Metal at runtime, the iOS target.
Both produce a static archive or `.so`/`.dylib`. Vendoring path: a `mel_prebuilt`-peer that fetches from a **Melody-hosted artifact mirror** (we cross-build once per Slang pin, publish, then fetch like the desktop zips) — chosen over building-from-source-in-the-consumer-build because the source build needs a host generator pass that does not fit the per-target build graph cleanly. The mirror URL + sha256 lives next to the pin.

**wasm.** No native static lib historically; the release artifact is `slang-wasm.zip` — full compiler compiled by Emscripten via embind, **~5 MB compressed** wasm. A native static-lib artifact (`slang-wasm-libs.zip`, headers + archives for C++ linkage) landed via issue #9486 / PR #9564 (Q1-2026 milestone), which is what we want: link libslang into our own `wasm` module rather than bridging through embind JS. **Single-threaded** Emscripten build (no SharedArrayBuffer dependency → no COOP/COEP requirement); compile must not block the browser main thread. Our §3.3 reactor-core model already handles this: the compile runs as a thread-callback-bridged completion (Web Worker on wasm), resolving on the device reactor — never a synchronous main-thread block. The `*_sync` wrapper debug-asserts on the reactor thread (§3.3) which forbids the deadlocking pattern by construction.

**Binary-size budget (MEL-ENGINE-VI).** libslang is large (full LLVM-class compiler). Per-platform ceilings, enforced as a build warning when the linked artifact exceeds them:

- desktop (win/linux/macos): no ceiling — disk-cheap, the runtime compiler is the whole point.
- wasm: ~5 MB compressed is the headline cost; budget ≤ 8 MB compressed including spirv-tools; gz-served. Lazy-load the wasm module (not blocking first paint) and cache it in the browser HTTP cache.
- android/ios: budget the stripped libslang archive ≤ 25 MB per arch (arm64 only shipped). This is the MEL-ENGINE-I acceptance Gabbo signed: mobile pays the binary-size cost for runtime parity. Mitigation: strip, LTO, dead-strip unused targets at link (we only need SPIRV+MSL emit on mobile — see §2 target gating), and persistent-cache so the compiler runs once per shader per install, not per launch (battery).

`third-party/slang/build.c` gains the android/ios/wasm `mel_prebuilt` arms (mirror URLs) and gates emit-targets per platform so the link dead-strips unreachable downstream paths.

## 2. `mel_slang_compile` target extension + DXIL signing

Extend `Mel_Slang_Target` (`third-party/slang/include/slang/compile.h`) from {SPIRV, MSL} to **{SPIRV, MSL, DXIL, WGSL}** (closed graphics-API protocol set — MEL-CODE-001 sanctioned, mirrors `Mel_Gpu_Shader_Target`). The wrapper (`src/compile.cpp`) maps each to the Slang `TargetDesc.format` + profile:

- SPIRV → `SLANG_SPIRV`, `spirv_1_5`, `-fvk-use-entrypoint-name`.
- MSL → `SLANG_METAL`, `metal`.
- WGSL → `SLANG_WGSL`, `wgsl`.
- DXIL → `SLANG_DXIL`, `sm_6_x`.

`Mel_Slang_Blob` carries text-vs-binary correctly (MSL/WGSL NUL-terminated text; SPIRV/DXIL raw bytes) as it already does for MSL.

**DXIL signing resolution (research-grounded).** Slang does **not** emit DXIL itself — it emits HLSL and hands off to **DXC** (`dxcompiler.dll`) as a downstream compiler; the result is **signed by `dxil.dll`**, a proprietary blob D3D12 requires for non-developer-mode shader creation. `dxil.dll` ships Windows (x64/arm64) + Linux (x64) only; no macOS, no Linux-arm64, no wasm. Resolution:

- **DXIL only matters on Windows** (D3D12 exists nowhere else). On the win32 target, libslang loads the co-located `dxcompiler.dll` + `dxil.dll` at runtime and produces **signed** DXIL — runtime signing works. We vendor `dxcompiler.dll` + `dxil.dll` alongside libslang on win32 (Microsoft.Direct3D.DXC NuGet payload; the OS-bundled DXC is too old for SM 6.5+). `caps.shader.bytecode_passthrough.dxil` stays true on D3D12 only when both DLLs resolved at device init; else the engine loud-fails DXIL shader creation (MEL-ENGINE-VIII) rather than minting unsigned blobs the runtime rejects with the opaque "corrupt or unrecognized format" error.
- Fallback when `dxil.dll` is absent (developer machine without the signer): emit **experimental-SM / unsigned** DXIL usable only under Windows Developer Mode, behind an explicit `device.debug.allow_unsigned_dxil` opt-in — never the default. Default is loud failure.
- Non-Windows builds never request DXIL; the target is dead-stripped from libslang there (§1 emit-target gating).

## 3. Runtime GPU API

### 3.1 Native-target selection

The engine maps the device's `caps.shader.bytecode_passthrough` to one `Mel_Slang_Target`, deterministically (no silent default — MEL-CODE-007):

- `msl` → MSL (metal backend).
- `spirv` → SPIRV (vulkan backend).
- `wgsl` → WGSL (webgpu backend).
- `dxil` → DXIL (d3d12 backend; requires the signer per §2).

If the device advertises none, shader creation loud-fails `MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED`.

### 3.2 API surface (`gpu/shader.h`, `gpu/pipeline.h`)

```
typedef struct {
    const char* source;          // NUL-terminated .slang text (from #embed)
    const char* vertex_entry;    // graphics: vs entry
    const char* fragment_entry;  // graphics: fs entry
    const char* compute_entry;   // compute: cs entry (mutually exclusive with vs/fs)
    const Mel_Gpu_Slang_Define* defines; u32 define_count;  // optional preprocessor defines
    const char* name;
} Mel_Gpu_Shader_Slang_Opt;

Mel_Gpu_Shader_Create_Future mel_gpu_shader_create_from_slang_opt(Mel_Gpu_Device*, Mel_Gpu_Shader_Slang_Opt);
#define mel_gpu_shader_create_from_slang(dev, ...) /* compound-literal wrapper */
```

A pipeline-from-slang convenience composes source-compile + `pipeline_create` into one future, deriving vertex layout / push-constant size / binding layout from Slang reflection (§6.4 reflection-driven layouts) so the screen stops hand-declaring layouts:

```
Mel_Gpu_Pipeline_Create_Future mel_gpu_pipeline_create_from_slang_opt(Mel_Gpu_Device*, Mel_Gpu_Pipeline_Slang_Opt);
```

`Mel_Gpu_Pipeline_Slang_Opt` carries the source + entries + the non-reflectable pipeline state (topology, blend, depth/stencil, color/depth formats, samples) that reflection cannot supply. Reflection JSON from libslang (the wrapper already runs the reflection request in `gen_bundles.sh`'s path — lift it into `mel_slang_compile`'s output) fills vertex elements, push-constant size, and bind-group layout.

### 3.3 Async model (§3.3)

Compile is **CPU-bound** → dispatched to the **job system**, completion **bridged to the device's target reactor** via `mel_reactor_post` (§3.3 "thread-callback bridged"). The future carries the `Mel_Gpu_Shader` / `Mel_Gpu_Pipeline` slot (rolled generation, U1) and a continuation. Three consumer forms (§3.3): coroutine `await`, continuation-callback, and `*_sync` (off-reactor threads / tooling only; reactor-thread call debug-asserts). On wasm the "job" is a Web Worker tick bridged identically — single-threaded compile never blocks the main thread.

Not cancellable in M1-M2 (§3.3); a future awaiting a destroyed device resolves Error/`device_lost`.

### 3.4 Persistent cache + compile-ahead

**Why.** First-compile latency of a full Slang compile is non-trivial (LLVM-class). Without a cache, every launch recompiles every shader → first-frame stall + battery (MEL-ENGINE-VI).

**Two-tier, matching Slang's own facilities:**
1. **In-process session cache** — one `ISession` per device, reused across compiles; libslang caches loaded modules + per-target code within the session. **Caveat (research):** the session cannot reuse across distinct `#define` configurations — distinct define sets need distinct sessions. The engine keys a small session pool by the define-set hash.
2. **Persistent on-disk cache** — content-addressed blob store keyed by the fingerprint below, holding the **emitted native blob** (SPIRV/MSL/WGSL/DXIL) + reflection. Lookup precedes any compile. We do *not* rely on Slang's `.slang-module` IR-serialization path for the persistent tier (it has an open null-deref bug discovering bare `.slang-module` dirs as of v2026.2, and target-codegen still runs on link); we cache the **final native blob** ourselves, which is both faster on hit and sidesteps that bug. The `.slang-module` IR cache is a future additive optimization for cross-shader module reuse, not the M1 mechanism.

**Cache key (fingerprint).**
```
sha256( slang_version_string
      ‖ source_hash            // sha256 of the embedded .slang bytes
      ‖ entry_name
      ‖ stage
      ‖ target                 // SPIRV|MSL|WGSL|DXIL
      ‖ sorted(defines)        // name=value pairs, canonical order
      ‖ target_profile )       // e.g. spirv_1_5 / sm_6_6 — pins emit ABI
```

**Invalidation** is automatic: any field change (Slang upgrade, source edit, different define set, different target) yields a different key → miss → recompile → new entry. No explicit invalidation API; stale entries are GC'd by LRU size cap. The Slang version in the key makes a compiler bump deterministically miss the whole cache rather than reuse a possibly-miscompiled blob (§6.4 "Slang version is part of the … cache key").

**Cache store per platform:**
- desktop/mobile: a directory under the app's per-user cache path (platform `storage` module), temp-file-plus-`rename` atomic writes, one file per binary (the §6.5 defensive-load discipline applies — header magic + hash validated on load, any mismatch is a miss).
- wasm: **IndexedDB** (persists across page loads; the emscripten FS `IDBFS` is the bridge, or a direct IndexedDB blob store keyed by the fingerprint string). The browser HTTP cache covers the wasm module itself; IndexedDB covers the compiled shader blobs.

**Compile-ahead-at-load.** A screen declares its shader set at init; the host kicks all compiles as a batch of futures at load (off the frame loop, §3.3 pump cadence decoupled from frame), populating the cache and shader slots before first frame. The render path awaits the pipeline future; a not-yet-ready pipeline either (a) blocks the screen's first draw until resolved or (b) renders a clear-only placeholder frame, the screen's choice. This is the §6.5 background-compile discipline that "eliminates the canonical first-frame pipeline-compile hitch."

## 4. `#embed` authoring

Each effect is one `apps/hello-gpu/shaders/slang/<effect>.slang`, multi-entry (`[shader("vertex")] vs_main`, `[shader("fragment")] fs_main`, or `[shader("compute")] cs_main`). The screen embeds it as a NUL-terminated string:

```c
static const char EFFECT_SLANG[] = {
#embed "shaders/slang/effect.slang"
    , 0
};
```

The build adds the `shaders/` dir to the embed search path (`-embed-dir` / include-path equivalent in `apps/hello-gpu/build.c`). C23 `#embed` is available on the repo's clang toolchains (host clang, NDK clang, emscripten clang, MSVC-clang on win32). No generator, no committed blob, no `gen_bundles.sh`. The screen passes `EFFECT_SLANG` to `mel_gpu_shader_create_from_slang`.

**Retired:** `apps/hello-gpu/src/*_spv.h`, `*_bundle.h`, `bundle_select.h`, `apps/hello-gpu/shaders/gen_bundles.sh`, the `*.{vert,frag,comp}` GLSL originals (after the §6 port lands), and `tools/build/vendor/slang/SLANG_VERSION.lock`'s role as a *generator* gate (the version pin survives as the cache-key + vendoring pin).

## 5. Parity prerequisites (reference — implemented separately)

These are not in this lane but are hard prerequisites for full per-backend parity; flagged so the port plan (§6) sequences behind them:

- **Metal compute encoder.** Several screens are compute (mandelbrot, boids, plasma, reacdiff, particles, bloom, clear, cull, shade…). The metal backend needs `MTLComputeCommandEncoder` dispatch + storage-image/storage-buffer binding for these to run. Reference: the metal backend's compute path.
- **Metal bindless.** The bindless screens (blit, mandelbrot, bloom — `u_textures[]`/`u_images[]` with `NonUniformResourceIndex`) need Metal argument-buffer bindless. The `gen_bundles.sh` WARNs already prove plain slangc rejects bindless non-uniform indexing without the `melody.binding` mixin (§6.4); the mixin / argument-buffer lowering is the gating work. Reference: `gpu-bindless-growable.md`.

The runtime-Slang lane lands the source-compile + cache + async machinery independently; screens needing compute/bindless on metal render once those backend paths exist.

## 6. ~37-shader Slang port plan

Single-source per effect, HLSL-superset translation from the GLSL originals. Inventory: 14 `.comp`, 13 `.frag`, 10 `.vert` GLSL + 5 already-authored `.slang` (triangle, blit, gradient, quad, clear). A vert+frag pair for one screen collapses to **one** `.slang` (two `[shader]` entries); a comp becomes one `.slang` (one entry).

**Translation rules (GLSL → Slang/HLSL-superset):**
- `vec/mat/ivec/uvec` → `float/int/uint` vector types; `mix`→`lerp`, `fract`→`frac`, `mod(x,y)`→`fmod` or `x - y*floor(x/y)` for negative-correct, `texture(s,uv)`→`t.Sample(s,uv)`, `imageStore`→`RWTexture2D[]` store, `gl_GlobalInvocationID`→`SV_DispatchThreadID`, `gl_VertexIndex`→`SV_VertexID`, `gl_Position`→`SV_Position`.
- `layout(push_constant) uniform Root{...}` → global `[[vk::push_constant]] Root root;` struct (§6.4 — one shared range across stages; per-entry push-constant offset-0 limitation, Slang #9643).
- `layout(set=0,binding=N) ... u_x[]` + `nonuniformEXT(i)` → `[[vk::binding(N,0)]] Texture2D u_x[];` + `NonUniformResourceIndex(i)` (the `blit.slang`/`mandelbrot` bindless shape already in-tree).
- `local_size_x/y` → `[numthreads(x,y,1)]` on the compute entry.

**Sequencing (incremental, triangle-shape first — recommended D3 from the prior spec):**
1. Non-bindless graphics (no compute, no bindless): triangle (done-shape), gradient, quad, instances, cube, lorenz, scene3d, star, cells, passthrough, layers, gallery, texquad, depth3d, depth-only, shadow (depth+scene), msaa. These render on **all four backends** the moment runtime-Slang lands.
2. Compute, non-bindless: clear, cull, buildargs, shade, dispatch-indirect, particle-sim/draw, boids-sim/draw, reacdiff-init/step/draw, plasma. Render on vulkan/d3d12/webgpu immediately; **metal once the compute encoder lands** (§5).
3. Bindless (graphics or compute): blit, mandelbrot, bloom (scene/bright/blurx/blury/composite), raymarch, postprocess/post, prepass. Render on vulkan/d3d12/webgpu; **metal once argument-buffer bindless lands** (§5).

Each ported `.slang` is verified by compiling to all four targets at build-or-test time (a `mel_add_test` that runs `mel_slang_compile` over every effect × every target the platform can emit, asserting non-empty blob + empty diagnostics) — the runtime analog of the old `gen_bundles.sh` cross-emit, now a gate not a generator.

## 7. Failure modes ⇒ answers

- **Compile failure.** libslang diagnostics surface verbatim in the future's Error status (`Mel_Slang_Blob.diagnostics`), logged loud (MEL-ENGINE-VIII). No partial pipeline, no silent fallback to a different target. The cross-emit test (§6) catches it at gate time, not in the field.
- **First-frame stall.** Async compile (§3.3) + persistent native-blob cache (§3.4) + compile-ahead-at-load. Warm cache → lookup only, no compile. Cold cache → background compile, screen renders placeholder until the pipeline future resolves; the frame loop is never blocked on a compile.
- **Binary size + mobile battery (MEL-ENGINE-VI).** Budgets in §1 (wasm ≤ 8 MB gz, mobile ≤ 25 MB/arch stripped+LTO+target-gated). Battery: persistent cache means the compiler runs once per shader per install, not per launch; the lazy wasm module load keeps it off the critical path.
- **Cache invalidation.** Pure content-addressing (§3.4 key includes Slang version, source hash, defines, target, profile). No manual invalidation; a changed input is a different key. LRU size cap GCs stale entries.
- **wasm single-threaded compile + IndexedDB persistence.** Single-threaded libslang runs in a Web Worker, bridged to the reactor (§3.3); never blocks main thread. Compiled blobs persist in IndexedDB keyed by the fingerprint; the wasm module itself rides the HTTP cache. `*_sync` debug-asserts on the reactor thread, forbidding the deadlock pattern by construction.
- **bundles → runtime migration.** Per-effect, atomically: author/port the `.slang` (§6), switch the screen from `mel_bundle_select_graphics(&bundle)` to `mel_gpu_shader_create_from_slang(EFFECT_SLANG, ...)`, delete the effect's `*_spv.h`/`*_bundle.h`. `bundle_select.h` and `gen_bundles.sh` are deleted once the last screen migrates. No flag-day: a half-migrated tree builds (migrated screens use runtime-Slang, un-migrated still embed bundles) until the last screen flips.
- **gpu-scene goldens extend to the new screens.** Each newly-rendering (screen, backend) pair gets a golden capture in the existing gpu-scene golden harness; the cross-emit test (§6) gates compilation, the golden gates pixels. A screen that renders on a backend it could not before (e.g. every compute screen on metal post-encoder) adds its golden in the same PR that unblocks it.

## 8. Open questions (genuine forks — Gabbo)

1. **android/ios libslang distribution.** Cross-build-and-mirror (we host the artifact, consumer build fetches like desktop) vs. source-build-in-build-graph (needs a host generator pass that does not fit the per-target graph). Spec assumes mirror; confirm we're willing to host + maintain the mirror per Slang pin.
2. **wasm libslang linkage.** Link the native `slang-wasm-libs` archive into our own wasm module (preferred; needs the #9486/#9564 artifact to be stable on our pin) vs. bridge through the embind JS surface (avoids the native-link dependency but couples to JS glue). Spec assumes native link; confirm the artifact is available at our pinned version.
3. **DXIL signer vendoring.** Ship `dxcompiler.dll` + `dxil.dll` (Microsoft.Direct3D.DXC payload) alongside libslang on win32 (license: the DXC redistributable terms apply). Confirm we accept redistributing Microsoft's signer blob, else d3d12 falls to unsigned-DXIL-under-Developer-Mode only.
4. **Reflection-driven layouts vs. retain hand-declared.** §3.2 proposes lifting Slang reflection into `mel_slang_compile`'s output to auto-derive vertex/binding/push-constant layout. Adopt now (single source of truth, §6.4 default) vs. keep per-screen hand layouts through the port and adopt reflection after. Spec assumes adopt-now for the pipeline-from-slang convenience; the bare shader-from-slang path works either way.
5. **Mobile runtime-compile acceptance restated.** §1 budgets ≤ 25 MB/arch for libslang on mobile. This is the MEL-ENGINE-I cost Gabbo signed. Confirm the budget; if a leaner mobile story is later wanted, the only lever is precompiled-cache-shipping (ship a warm cache in the app bundle so the compiler need never run on-device for the shipped shader set) — a future additive, not a retreat from runtime-everywhere.

## Implementation breakdown (ordered; no-prerequisite first)

**No prerequisites:**

- **I1. Extend `mel_slang_compile` targets.** Add `MEL_SLANG_TARGET_DXIL`, `MEL_SLANG_TARGET_WGSL` to `compile.h`; wire `TargetDesc.format`/profile in `compile.cpp` for all four; text-vs-binary blob handling for WGSL (text) and DXIL (binary). Extend `test/compile_test.c` to cover four targets (gated to host-emittable ones). *(third-party/slang)*
- **I2. Reflection in the compile output.** Add a reflection-JSON field to `Mel_Slang_Blob` (or a sibling `mel_slang_reflect`); lift the `-reflection-json` request from `gen_bundles.sh` into the wrapper. Emit vertex inputs, push-constant size, binding set/slot/type. *(third-party/slang)*
- **I3. Cache-key + blob store.** Content-addressed store: fingerprint hash (§3.4), atomic temp+rename writes, defensive-load header (§6.5), LRU cap. Desktop/mobile dir backend first. *(modules/gpu, new internal unit)*

**Depends on I1:**

- **I4. `mel_gpu_shader_create_from_slang` (sync core).** Native-target selection from caps (§3.1); compile via `mel_slang_compile`; feed `shader_create_from_bytecode`. Synchronous first (no future yet) to prove the path. Loud-fail on no-passthrough-cap and on compile error. *(modules/gpu/shader)*

**Depends on I3, I4:**

- **I5. Async future + job bridge.** Wrap I4 in `Mel_Gpu_Shader_Create_Future`; dispatch compile to the job system; bridge completion to the device reactor (§3.3). Coroutine + callback + `*_sync` (reactor-thread assert) forms. Wire cache lookup before dispatch. *(modules/gpu/shader, modules/gpu/future)*

**Depends on I2, I4:**

- **I6. `mel_gpu_pipeline_create_from_slang`.** Compose compile + reflection-derived layout + `pipeline_create`; carry non-reflectable pipeline state in the opt. Async via I5's future shape. *(modules/gpu/pipeline)*

**Depends on I4 (one screen):**

- **I7. Migrate `triangle.c` to `#embed` + `mel_gpu_shader_create_from_slang`.** Proves `#embed` of the in-tree `triangle.slang`, retires `triangle_spv.h`/`triangle_bundle.h` for that screen. Embed search-path wiring in `apps/hello-gpu/build.c`. *(apps/hello-gpu)*

**Depends on I1 (build wiring, parallelizable with I4-I6):**

- **I8. android/ios/wasm libslang vendoring.** `mel_prebuilt`-peer arms in `third-party/slang/build.c` for the three platforms (mirror URLs + sha256, per OQ1/OQ2); emit-target gating per platform (mobile: SPIRV+MSL only; wasm: WGSL+SPIRV; strip non-target downstream paths). win32: vendor `dxcompiler.dll`+`dxil.dll` next to libslang (per OQ3). Size-budget build warning (§1). *(third-party/slang)*
- **I9. wasm IndexedDB cache backend.** IDB blob store keyed by the fingerprint string; emscripten `IDBFS` or direct IndexedDB. Behind I3's store interface. *(modules/gpu, wasm)*

**Depends on I5, I6, one ported batch:**

- **I10. Compile-ahead-at-load in gpu_host.** Screen declares its shader set; host batches the compile futures at init off the frame loop; render path awaits the pipeline future (placeholder-frame policy). *(apps/hello-gpu/gpu_host)*

**Depends on I4-I6, sequenced per §6 (largest, splittable per batch):**

- **I11a. Port + migrate batch 1** (non-bindless graphics — renders all four backends). Delete each screen's `*_spv.h`/`*_bundle.h`.
- **I11b. Port + migrate batch 2** (compute, non-bindless — gated on metal compute encoder for metal pixels).
- **I11c. Port + migrate batch 3** (bindless — gated on metal argument-buffer bindless for metal pixels).
- **I11z. Retire** `bundle_select.h`, `gen_bundles.sh`, remaining GLSL `*.{vert,frag,comp}` once the last screen migrates. *(apps/hello-gpu)*

**Depends on I11 batches:**

- **I12. Cross-emit gate + goldens.** `mel_add_test` compiling every effect × every host-emittable target (non-empty blob, empty diagnostics). Extend gpu-scene golden harness with the newly-rendering (screen, backend) pairs. *(apps/hello-gpu, tools/test)*
