# Runtime Slang → GPU API (task #32, I4-I7)

## Work done

Added a runtime shader/pipeline-from-Slang path to the `gpu` module and proved it: the
triangle renders from ONE `#embed`'d `triangle.slang`, compiled at runtime to the device's
native target, on Vulkan, Metal, and WebGPU — diffed bit-for-bit against the shared
`shared/triangle` golden.

### API surface (sync, this milestone)

`modules/gpu/include/gpu/shader.h`:
- `Mel_Gpu_Shader_Slang_Opt { source, vertex_entry, fragment_entry, compute_entry, name }`
  + `mel_gpu_shader_create_from_slang_opt` (compound-literal macro
  `mel_gpu_shader_create_from_slang`). Compute path taken when `compute_entry` is set;
  graphics path needs both vertex+fragment entries.
- `mel_gpu_slang_target_for_device(dev, &out_target)` — exposes the cap→target mapping.
- Pulls `<slang/compile.h>` into the public gpu header (gpu now depends on slang
  unconditionally, so this is sound).

`modules/gpu/include/gpu/pipeline.h`:
- `Mel_Gpu_Pipeline_Slang_Opt` — source + vertex/fragment entries + the *non-reflectable*
  pipeline state (topology, cull, formats, blend, depth/stencil, samples, vertex_buffers,
  bindless, set_layouts, static_samplers, spec_constants). Reflection supplies the rest.
- `Mel_Gpu_Pipeline_Compute_Slang_Opt` — source + compute entry + push-const/bindless/sets/specs.
- `Mel_Gpu_Pipeline_From_Slang_Result { value (pipeline), shader, status }` — surfaces BOTH
  handles. The convenience creates the shader internally; the caller owns both and destroys
  both (mirrors the established screen lifetime: shader outlives pipeline create, both freed
  at teardown). On failure the internal shader is destroyed before return; on success it is
  handed back via `.shader`.
- `mel_gpu_pipeline_create_from_slang_opt` / `mel_gpu_pipeline_compute_create_from_slang_opt`
  (+ macros).
- **Additive** `u32 threadgroup[3]` on `Mel_Gpu_Pipeline_Compute_Opt` (Q2 wiring; see below).

### Implementation: `modules/gpu/src/slang.c` (new TU, backend-agnostic)

- **Native-target selection.** `mel_gpu_slang_target_for_device` reads
  `caps.shader.bytecode_passthrough` → exactly one `Mel_Slang_Target`
  (spirv→SPIRV, msl→MSL, wgsl→WGSL, dxil→DXIL). Each backend advertises exactly one
  passthrough flag (verified: vulkan spirv, metal msl, webgpu wgsl, d3d12 dxil; others
  explicitly false), so the order is unambiguous. If none, loud-fail and return false →
  `MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED` (MEL-CODE-007, no silent default).
- **Compile + diagnostics.** `mel_slang_compile` / `mel_slang_compile_reflect` to that
  target. On `!blob.data`, `mel_log_error` surfaces `blob.diagnostics` verbatim and frees
  the blob (MEL-ENGINE-VIII). Graphics compiles vertex + fragment as two single-entry blobs;
  both fed to the existing `mel_gpu_shader_create_from_bytecode` (which is resolved at link
  to whichever backend is compiled in — no per-backend code touched).
- **Reflection-driven layout.** `mel_slang_compile_reflect` on the vertex stage yields
  `vertex_attrs` (location, format, offset) + `vertex_stride` + `push_constant_size`; the
  pipeline path builds the `Mel_Gpu_Vertex_Element[]` from them (heap allocator,
  `mel_alloc_heap()`, freed after pipeline create). Slang vertex format → `Mel_Gpu_Format`
  covers F32X2/3/4 (the only vertex formats the gpu format enum has); anything else (int/uint,
  scalar float) loud-fails with the attr name+location, since the gpu module has no such
  format — honest refusal, not a silent substitution.
- **Compute threadgroup.** Compute pipeline path reads reflection's `workgroup[3]` and passes
  it into `pipeline_compute_create` via the new `threadgroup[3]` field.

## The entry-name quirk (real find, load-bearing)

Slang's SPIR-V emit, WITHOUT `-fvk-use-entrypoint-name` (which the wrapper does not pass),
names the `OpEntryPoint` **`main`** regardless of the requested entry — verified by
disassembly (`OpEntryPoint Vertex %vs_main "main"`). MSL keeps `vs_main`, WGSL keeps
`fn vs_main`. The committed offline bundles were generated *with* that flag (entry `vs_main`),
so the runtime path initially failed `vkCreateGraphicsPipelines → VK_ERROR_INITIALIZATION_FAILED`
(pName `vs_main` not found). Fix lives entirely in `slang.c`:
`mel_gpu__downstream_entry(target, entry)` returns `"main"` for SPIRV and the original entry
otherwise. This is the correct owner of the quirk — the from-slang path knows the target and
the emit convention; the backends keep their existing entry-name contract.

## `#embed` migration

- `apps/hello-gpu/src/triangle.c`: `static const char TRIANGLE_SLANG[] = { #embed
  "shaders/slang/triangle.slang", 0 };`, then one `mel_gpu_pipeline_create_from_slang` call.
  No more `triangle_bundle.h` / `bundle_select.h`. Other screens stay on bundles.
- Embed search path: `--embed-dir=apps/hello-gpu` cflag in `apps/hello-gpu/build.c`. Verified
  empirically that clang `#embed` honors `--embed-dir` (NOT `-I`); the value is repo-root
  relative because ninja runs from the repo root.

## Link + loader

- `modules/gpu/build.c`: `mel_depends(lib, "slang")` (unconditional, runtime everywhere). The
  prebuilt mechanism already vendors `libslang.dylib` and stamps an `LC_RPATH` at
  `third-party/slang/build/.../prefix/lib` into every consumer; `libslang.dylib` resolves to
  `@rpath/libslang-compiler.0.2026.10.2.dylib`. So no loader env is needed for libslang. The
  `DYLD_LIBRARY_PATH=/opt/homebrew/lib` in the test command is for the Vulkan loader
  (MoltenVK), unrelated to slang.

## The proof (gpu-scene)

`scene_shared/triangle` in `test_scene.c` now `#embed`s `triangle.slang` and renders via
`mel_gpu_pipeline_create_from_slang` (embed-dir cflag added to the gpu-scene target). Same
golden, runtime compile per backend:

- `gpu-scene macos --gpu=vulkan`  → **4/4** (SPIR-V, runtime).
- `gpu-scene macos --gpu=metal`   → **4/4** (MSL, runtime).
- `gpu-scene macos --gpu=webgpu`  → **3/4** (WGSL, runtime; quad skip — WebGPU core has no
  push constants). The WebGPU pass is the NEW validation: Dawn/tint rejects malformed WGSL, so
  a green `scene_shared.triangle` on WebGPU proves the wrapper's WGSL emit is real.

Regressions held: `gpu-vulkan` 48/48, `gpu-metal` 8/8, `gpu-foundation` 13/13,
`gpu-resources` 4/4, `slang-compile` 10/10. `hello-gpu` builds on vulkan/metal/webgpu.

## Kludges / debt (MEL-ENGINE-VIII — full account)

1. **Metal Q2 only half-closed.** The `threadgroup[3]` field now *reaches* the compute opt
   (additive) and the from-slang path fills it from reflection's numthreads. But the metal
   backend (`src/metal/macos/pipeline.m:644`) still hardcodes
   `MTLSizeMake(threadExecutionWidth, 1, 1)` and does NOT read the new field — touching
   `src/metal` is outside this task's ownership. The 3-D threadgroup arrives at the API
   boundary; the metal dispatch consuming it is the one-line backend follow-up. Vulkan/D3D12
   derive numthreads from the shader bytecode, so they are unaffected.

2. **test_scene.c migrated outside the stated file-ownership list.** The task's VERIFY
   *requires* the gpu-scene triangle to compile `triangle.slang` at runtime; that scene is
   self-contained in `test_scene.c` (it does NOT route through the app's `triangle.c`). The
   proof is impossible without migrating that one scene, so I migrated *only* the
   `scene_shared/triangle` test, surgically (re-applied by hand to avoid clang-format churn on
   neighbouring tests). `triangle_multistream` (which reuses the same golden but needs a
   multi-buffer split reflection cannot derive) and every other scene stay on bundles.

3. **`from_slang` allocations use `mel_alloc_heap()`, not a param allocator.** The transient
   vertex-layout array uses the heap allocator (as `future.c` does), because the public gpu
   create API takes no allocator param. Consistent with the module; if the gpu API later
   threads a device/frame allocator, this should follow. Not a new pattern, but flagged
   against MEL-CODE-003's strict reading.

4. **Per-call global session.** Each `mel_slang_compile[_reflect]` spins up a fresh
   `IGlobalSession`/`ISession` inside the wrapper (full LLVM-class compiler init per stage).
   The triangle pays this twice (vs+fs) at create. Acceptable for this sync milestone; the
   §3.4 session pool + content-addressed cache are the immediate follow-up (deferred, below).

5. **`#embed` `, 0` split across two lines** by clang-format (`, ` then `0`). Cosmetic; the
   array is still a NUL-terminated source string.

## Deferred (explicitly out of this milestone, per the task)

- **Async (§3.3).** SYNC for this milestone. No `Mel_Gpu_*_Create_Future`, no job-bridge, no
  `*_sync` reactor-assert. The from-slang functions return results directly, matching the
  existing `*_opt` shape.
- **Content-addressed cache + session pool (§3.4, I3/I5).** Not implemented; the fingerprint
  store, atomic temp+rename, LRU, and per-define session keying are the next unit. Every
  create currently recompiles.
- **DXIL / d3d12.** No d3d12 device on this host; the target mapping and downstream-entry
  logic include DXIL (entry kept as-authored, like MSL/WGSL), but it is unexercised here.
- **android/ios/wasm vendoring (I8), IndexedDB cache (I9), compile-ahead (I10), the ~37-shader
  port (I11)** — separate lanes.

## Open questions for Gabbo

1. **Metal Q2 last mile.** Do you want me (or the metal-compute owner) to land the one-line
   `obj.threadgroup = MTLSizeMake(opt.threadgroup[0|1|2])` read in `pipeline.m`, gated on a
   non-zero field (zero → keep the current `threadExecutionWidth` fallback, so it stays
   additive and non-breaking)? It's blocked only by the src/metal ownership fence.
2. **Pipeline-from-slang result shape.** I surfaced the created shader via
   `Mel_Gpu_Pipeline_From_Slang_Result { value, shader, status }` so the caller owns both
   lifetimes. Alternative: have the pipeline *own* the embedded shader and free it on
   `pipeline_destroy` (single handle, but a backend change to track ownership). The current
   shape needs no backend change and composes with the existing destroy calls — confirm it's
   the one you want before the async/cache lane builds on it.
3. **Vertex-format coverage.** The gpu `Mel_Gpu_Format` enum has only F32X2/3/4 vector vertex
   formats (no scalar float, no int/uint). The from-slang layout loud-fails on anything else.
   When a ported screen needs e.g. a `uint` vertex attribute, the gpu format enum must grow
   first. Flagging so the port plan sequences that.

## CLAUDE.md suggestions (recommendations only)

- Document `--embed-dir` as the sanctioned way to make `#embed` resolve repo-root-relative
  resources (it does NOT use `-I`), in `modules/build/platforms.md`, now that `#embed` is a
  first-class authoring path for shaders.

## Suggestions

- A `mel_add_test` cross-emit gate (§6/I12): compile every in-tree `.slang` × every
  host-emittable target, assert non-empty blob + empty diagnostics. It would have caught the
  `main`-vs-`vs_main` entry quirk at gate time rather than at pipeline-create.
