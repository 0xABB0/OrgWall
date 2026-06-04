# Slang wrapper: four backend ILs + reflection (I1 + I2)

Task #31, foundation of the runtime-Slang initiative (`design/gpu-runtime-slang.md`, items I1+I2).
File ownership respected: only `third-party/slang/{include/slang/compile.h, src/compile.cpp, test/compile_test.c, build.c}` touched. No `modules/gpu` / `apps` edits.

## Work done

### I1 — target extension + emit paths

- `Mel_Slang_Target` extended to `{SPIRV=0, MSL=1, DXIL=2, WGSL=3}` (SPIRV=0 preserved). Fixed graphics-API protocol set, MEL-CODE-001 sanctioned per the existing note.
- `mel_slang_compile` now maps each target to `TargetDesc.format` + profile via a single `mel_slang__map_target` table:
  - SPIRV → `SLANG_SPIRV` / `spirv_1_5`, binary.
  - MSL → `SLANG_METAL` / `metal`, text (NUL-terminated).
  - WGSL → `SLANG_WGSL` / `wgsl`, text (NUL-terminated).
  - DXIL → `SLANG_DXIL` / `sm_6_6`, binary — compiled **only** under `MEL_SLANG_EMIT_DXIL`.
- Text-vs-binary blob handling generalized from "is-MSL" to the table's `text` flag, so WGSL gets a trailing NUL like MSL and SPIRV/DXIL stay raw bytes.
- **DXIL gating.** Off-win32 the DXIL arm is `#if defined(MEL_SLANG_EMIT_DXIL)`-compiled-out; a DXIL request returns `data=NULL`, `diagnostics="slang: DXIL unavailable off-win32 (D3D12 + dxcompiler/dxil signers are win32-only)"` — loud fail, no silent fallback (MEL-ENGINE-VIII, MEL-CODE-007). `build.c` defines `MEL_SLANG_EMIT_DXIL=1` only `WHEN(.platforms = MEL_ON(WIN32))`, public visibility so dependents gate on the same condition. This is the §1/§2 emit-target dead-strip.

### I2 — reflection in the compile output

- New paired call `mel_slang_compile_reflect(source, entry, stage, target, Mel_Slang_Reflection* out)`; `mel_slang_compile` delegates to it with `out=NULL`. Reflection is opt-in (NULL skips it), populated only on compile success, freed via `mel_slang_reflection_free` regardless. This keeps the original signature byte-compatible for the (currently zero) consumers while adding the layout surface the pipeline lane needs.
- `Mel_Slang_Reflection` carries, per entry point:
  - `entry` (reflected name), `stage`, `is_compute`.
  - `vertex_attrs[]` (dynamic): `semantic`, `location` (varying-input offset), `format` (`Mel_Slang_Vertex_Format`), per-attribute `offset` + `size`, plus the implied interleaved `vertex_stride`. Struct vertex inputs are flattened recursively in declaration order.
  - `bindings[]` (dynamic): `name`, `kind` (`Mel_Slang_Resource_Kind`: uniform-buffer / sampled-texture / storage-texture / sampler / storage-buffer), `set`, `slot`, `count` (0 = unbounded/bindless array), and `size` for uniform buffers.
  - `push_constant_size` (bytes of the `[[vk::push_constant]]` range).
  - `workgroup[3]` for compute `[numthreads]`.
- Sourced from libslang's `IComponentType::getLayout(0)` → `ProgramLayout`:
  - entry point found by name via `getEntryPointByIndex` scan.
  - vertex attrs from `EntryPointReflection::getParameterByIndex` → `TypeLayoutReflection` field walk; format from `TypeReflection` scalar type + component count; location from `getOffset(VARYING_INPUT)`.
  - bindings from `ProgramLayout::getParameterByIndex` (global params), `kind` projected from `TypeReflection::Kind` + resource shape/access; `set = getBindingSpace() + getOffset(REGISTER_SPACE)`, `slot = getBindingIndex()`.
  - push-constant detected via `PushConstantBuffer` category, size from the element type layout's uniform size.
  - numthreads from `EntryPointReflection::getComputeThreadGroupSize(3, …)`.

### I3 — per-platform emit-target gating

Implemented as the win32-only `MEL_SLANG_EMIT_DXIL` define in `build.c`; the wrapper's DXIL codegen path compiles out elsewhere. The broader mobile/wasm narrowing (SPIRV+MSL on mobile, WGSL+SPIRV on wasm) is **not** wired here — it depends on the android/ios/wasm `mel_prebuilt` arms (design item I8), which are out of this task's scope. Only DXIL has a concrete, verifiable gate today.

## Test results (`./nob test slang-compile macos`, 10/10 pass)

- **SPIRV** vertex + fragment: SPIR-V magic `0x07230203` + 4-byte word-count alignment asserted. **Validated** (magic + structure).
- **MSL** vertex: `metal_stdlib` present, and the emitted source **compiles** via `xcrun -sdk macosx metal -c` — printed `MSL verified`. **Validated** against the real Metal toolchain.
- **WGSL** vertex + compute: non-empty, NUL-terminated, `fn `/entry-name structural check. **Unverified** by tint/naga (neither in the macOS toolchain); printed `WGSL UNVERIFIED … structural well-formedness only`. The codegen target exists in this libslang pin and produces output; semantic validity is unconfirmed.
- **DXIL** off-win32: request loud-fails with the `DXIL unavailable off-win32` diagnostic, asserted. Runtime DXIL emit (signed, via dxcompiler/dxil) is **verified later on win-pilot** — cannot be emitted on macOS by construction.
- **Reflection** assertions all matched libslang exactly:
  - vertex: 3 attrs (F32X3 pos @0, F32X3 col @12, F32X2 uv @24), stride 32, semantic `POSITION`; push-constant size 80 (float4x4 64 + float4 16).
  - fragment: 1 sampled-texture + 1 sampler binding.
  - compute: `is_compute`, workgroup {8,4,2}, 1 storage-buffer binding.

`./nob build slang macos` and `./nob build slang-compile macos` both clean (exit 0). No consumers of the wrapper exist outside `third-party/slang`, so nothing downstream is affected.

## libslang API notes / surprises

- **Library rename confirmed.** The 2026.10.2 macOS release ships `libslang-compiler.0.2026.10.2.dylib` with a `libslang.dylib` compatibility symlink. `build.c`'s `mel_link(rt, …, "-lslang")` resolves against that symlink and links fine on this pin. When the symlink is dropped (design says end-2026), `-lslang` will break and the link flag must move to `-lslang-compiler` (or fully-versioned). Flagged below as debt.
- Reflection requires `getLayout` on the **linked** `IComponentType` (not the composite); on the unlinked composite the layout is incomplete.
- `set` from `getBindingSpace()` + `getOffset(REGISTER_SPACE)`: for the simple `[[vk::binding(n,space)]]` shaders tested, `getBindingSpace()` carried the space and the register-space offset was 0; the sum is defensive for nested parameter blocks. Unverified against multi-space/parameter-block shaders.
- Vertex `location` is taken as the varying-input category offset. For the tested tightly-packed inputs this equals the field index; libslang may assign non-contiguous locations for packed/array varyings — unverified beyond the 3-attr case.

## Kludges / debt (MEL-ENGINE-VIII, bar is zero)

1. **`malloc`/`realloc`/`free`, not an allocator (MEL-CODE-003 violation).** The wrapper allocates the blob, diagnostics, and the dynamic reflection lists with raw libc `malloc`/`realloc`, freed by `mel_slang_blob_free` / `mel_slang_reflection_free`. This is **pre-existing** — the original `mel_slang_compile`/`mel_slang_blob_free` already used `malloc`/`free` and the C ABI carries no allocator parameter. I extended the same discipline rather than redesign the ABI mid-task. Proper fix: thread a `Mel_Allocator*` through the compile/reflect calls (an ABI change touching the future I4 consumer). Deferred; should be decided with Gabbo before the gpu lane consumes the wrapper.
2. **`workgroup[3]` is a fixed-size array.** MEL-CODE-002 forbids fixed arrays, but compute thread-group arity is a hard protocol constant of 3 (X/Y/Z), matching libslang's own `getComputeThreadGroupSize(3, …)` — not an `[MEL_MAX_*]` ceiling that could ever need to grow. Judgment call; flagging it explicitly. If Gabbo wants it as `uint32_t workgroup_x/y/z` scalars instead, trivial change.
3. **`realloc`-per-element growth.** `mel_slang__collect_*` grow the reflection lists one element at a time via `realloc`. Correct but O(n²)-ish; reflection runs once per compile (cached downstream) over a handful of attrs/bindings, so the cost is negligible. Left simple.
4. **WGSL unvalidated.** No tint/naga in the macOS toolchain, so WGSL output is only structurally checked. A real WGSL validator (tint via dawn, or naga) in the test toolchain would close this. The codegen path works (produces output); semantic correctness is the open item.
5. **DXIL untested on macOS by construction.** Only the off-win32 loud-fail is asserted here; signed-DXIL emit is a win-pilot verification (the `dxcompiler.dll`/`dxil.dll` vendoring is design item I8, not this task).
6. **No new comments** per the hard rule; pre-existing comments in `build.c` preserved (not mine to churn).

## Open questions for Gabbo

1. **Allocator ABI.** Should `mel_slang_compile_reflect` take a `Mel_Allocator*` (fixing MEL-CODE-003 properly) before the I4 gpu consumer lands, or stay libc-`malloc` with a free-function pair? An ABI change is cheapest now while there are zero consumers.
2. **`set` derivation for parameter blocks / multi-space.** The `getBindingSpace() + getOffset(REGISTER_SPACE)` sum is unverified beyond single-space shaders. Confirm whether the gpu lane needs parameter-block (`ParameterBlock`/`set>0`) layouts reflected now, or later — the current code returns the flat space, which is correct for the in-tree `[[vk::binding(n,space)]]` shape but untested for nested blocks.
3. **`-lslang` link flag.** Survives only while the rename symlink exists. Should `build.c` move to `-lslang-compiler` now (matches the ≥v2025.21 reality) at the cost of breaking older pins, or keep `-lslang` until the symlink is removed?
4. **WGSL validation in CI.** Worth pulling tint/naga into the test toolchain to actually validate WGSL, or accept structural-only until the webgpu backend exercises it end-to-end?
