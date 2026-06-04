# Dual-lane bindless: Metal per-dispatch argument buffer, Vulkan/WGSL heap unchanged (task #37)

## Work done — what changed, and why

The mandelbrot/clear compute kernels indexed an unbounded device-global storage-image
heap (`[[vk::binding(4,0)]] RWTexture2D u_images[]`). Slang's MSL emit lowers that to a
flexible-array-member kernel parameter Metal's runtime compiler rejects ("flexible array
members are a C99 feature"). That gated mandelbrot-on-Metal (`MEL_SKIP`).

Per Gabbo's decision, this is solved by a DUAL-LANE authoring split, not the cross-backend
binding-model rewrite the prior checkpoint scoped. The §6.7 device-global heap stays
UNCHANGED on Vulkan/D3D12/WGSL (still green). On Metal only, Slang's first-class
`DescriptorHandle<T>` is used; it lowers to a per-dispatch argument buffer (no global heap
on Metal). One `.slang` source authors both lanes.

### The dual-lane shader authoring (the target split)

Slang predefines no per-target preprocessor macro (only `__HLSL__`/`__SLANG__`; probed the
vendored libslang 2026.10.2). `__target_switch` gates only function bodies, not top-level
declarations — but the two lanes differ in declarations (a `uint` slot + heap array vs a
`DescriptorHandle` field). So the wrapper now injects a per-target macro
(`MEL_TARGET_METAL`/`_SPIRV`/`_WGSL`/`_DXIL`) via `SessionDesc.preprocessorMacros`, and the
shaders `#if defined(MEL_TARGET_METAL)`:

- **Metal lane:** `DescriptorHandle<RWTexture2D<float4>> image` field in the push-constant
  `Root`; `mel_storage_image()` returns it (implicit `getDescriptorFromHandle`).
- **Vulkan/WGSL lane:** the original `uint image` + `[[vk::binding(4,0)]] u_images[]`;
  `mel_storage_image()` returns `u_images[root.image]` (clear keeps `NonUniformResourceIndex`).

Slang inlines `mel_storage_image()` away, so the Vulkan/WGSL emit is provably unchanged:
the dual-lane SPIR-V of both mandelbrot and clear is **byte-for-byte identical** to the
committed HEAD originals (verified with a standalone libslang dumper + `cmp`). WGSL emit is
also identical. The Metal lane's emitted MSL compiles to AIR via `xcrun metal -c`.

`apps/hello-gpu/shaders/slang/{mandelbrot,clear}.slang` were re-authored dual-lane.

### Metal per-dispatch argument buffer (+ #34 reuse)

The Metal `DescriptorHandle` lowering makes the whole `Root` ONE mixed argument buffer at
`[[buffer(0)]]`: the texture inlined as argument-buffer member 0, the scalars as inline
uniforms (member indices 1..N). The host builds this per dispatch:

- **Pipeline-create** (`pipeline.m`): when the from-slang glue supplies a bindless arg-field
  plan, build an `MTLArgumentEncoder` from the compute function's `[[buffer(0)]]` argument
  (`newArgumentEncoderWithBufferIndex:0`) and stash it + the field plan on the pipeline.
- **`cmd_push_constants`** (`record.m`): on the Metal compute encoder, if the bound pipeline
  has a plan, the bytes are stashed (deferred) instead of `setBytes`; loud error if the host
  push-constant span is smaller than the plan's host size.
- **`cmd_dispatch`** (`record.m`): build a transient argument buffer from the encoder — for
  each resource field, read its 4-byte slot from the stashed bytes, resolve slot → live
  resource (reusing the #34 registration), `setTexture:/setBuffer:/setSamplerState:atIndex:`,
  and `useResource:`; for each uniform field, `memcpy` host bytes to `constantDataAtIndex:`.
  Bind the argument buffer at `[[buffer(0)]]`, then dispatch.

The probed argument-encoder member index equals the Slang field index (texture=0,
scalars=1..N); `constantDataAtIndex:i` maps to the correct byte offset. The host slot→arg
mapping is computed from the Metal reflection (resource fields consume a 4-byte host slot,
uniform fields consume their byte size), walking fields with a host cursor.

**#34 reuse.** The existing `binding.m` slot==index heap registration
(gpuResourceID/gpuAddress writes + `MTLResidencySet`) is unchanged. A parallel slot→resource
side table (`Mel_Gpu_Bindless.resources[class]`, a dynamic per-class array sized to the heap
cap) was added alongside each register call so the argument encoder can fetch the live
`id<MTLResource>`/`id<MTLSamplerState>` (the encoder needs the object, not just its id). The
texture is already resident via the #34 residency set, so the inlined arg-buffer reference is
covered; `useResource:` is added defensively. The 5-heap manual path (test_metal) is intact
(gpu-metal 12/12).

### The from_slang glue (`slang.c`)

The Metal arg-buffer layout is detected from reflection: the wrapper's
`mel_slang__collect_metal_arg_buffer` walks the push-constant struct on the MSL target and
reports each field's role (resource vs uniform), host byte offset, argument-buffer member
index, byte size, and (for resources) the Slang resource kind. `slang.c` translates that to
`Mel_Gpu_Bindless_Arg_Field[]` and passes it (Metal-consumed, otherwise-ignored) through
`Mel_Gpu_Pipeline_Compute_Opt`. The screen `.c` (`test_scene.c`) passes slots + uniforms
abstractly — its `cmd_bind_bindless` / `cmd_push_constants` / `cmd_dispatch` sequence is
backend-agnostic and unchanged; the Metal MEL_SKIP was removed so the same code runs.

### PROOF

`gpu-scene --gpu=metal` mandelbrot now RENDERS (target 6/6; no longer MEL_SKIP). The
fractal output is diffed against the shared golden `golden/shared/mandelbrot.ppm` (minted by
the Vulkan oracle) through the RHI. Measured delta: **max channel delta 0, zero offending
pixels** — bit-identical to the golden (confirmed by a throwaway zero-tolerance run, then
reverted). The compute kernel is runtime-compiled MSL; the `DescriptorHandle` storage-image
resolves via the per-dispatch argument buffer through the compute encoder.

### Verification matrix (all green)

- gpu-scene macos --gpu=metal: **6/6** (was 5/6 + 1 skip).
- gpu-scene macos --gpu=vulkan: **6/6**, mandelbrot bit-identical SPIR-V → unchanged green path.
- gpu-scene macos --gpu=webgpu: 3/6 + 3 skip (unchanged; WebGPU core lacks push-constants /
  true bindless — mandelbrot hits the pre-existing bindless gate before any new code).
- gpu-metal: **12/12** (manual heap intact).
- gpu-vulkan: **48/48** (unchanged).
- slang-compile: **10/10**.
- hello-gpu macos vulkan/metal/webgpu: all link.

## Kludges / debt (MEL-ENGINE-VIII: confess all)

- **Transient per-dispatch argument buffer allocation.** Each `cmd_dispatch` on a from-slang
  Metal bindless pipeline allocates a fresh `MTLBuffer` of the encoder's `encodedLength` (40
  bytes for mandelbrot). Metal retains it for the command buffer's lifetime, so it is
  correct, but it is a per-dispatch allocation (MEL-ENGINE-III: a real cost, made visible).
  A pool of reusable arg buffers keyed by encoded length would amortize it; deferred — the
  scene dispatches once. Flagged.
- **"Bindless prelude" file not created.** The task listed a shared bindless prelude. The
  wrapper compiles a single source string with no include search path, so a separate
  `.slang` prelude cannot be `#include`d by the embedded shaders. Inlining the dual-lane
  block per shader (mandelbrot, clear) is the honest form; a dead, unincludable prelude file
  would be slop. The shared pattern lives here and in the design doc instead. Deviation from
  the literal deliverable, justified.
- **hello-gpu mandelbrot-on-Metal end-to-end not proven at runtime.** The PROOF is the
  compute kernel via gpu-scene (rendered, bit-identical). hello-gpu's on-screen present uses
  `bindless_present`/`blit.slang` via a PRECOMPILED bundle (not from_slang), a separate
  graphics sampled-texture+sampler bindless surface on the manual 5-heap lane — out of this
  task's scope (the per-dispatch arg buffer is from_slang-compute only). hello-gpu builds on
  all three backends; its Metal present path was not exercised here. Flagged.
- **Resource side-table memory.** The slot→resource reverse table is a dynamic array sized to
  the heap cap per class (e.g. 16384 pointers for storage-image). It mirrors the existing
  fixed-cap heap design (the cap is a runtime value, not a `[MEL_MAX_*]` literal, so
  MEL-CODE-002 holds). It costs one pointer per slot in addition to the 8-byte heap id.
- **clang-format not auto-applied.** The host's clang-format (22) disagrees globally with the
  committed code (e.g. it wants `extern "C"` brace-on-newline and `AlignConsecutiveAssignments:
  None` un-aligning, both contradicted by every committed file). Running it would churn
  unrelated code. New lines were hand-matched to the surrounding committed style; the
  only residual clang-format diffs are in pre-existing code, not in any added line.

## Wrapper change (FLAGGED)

`third-party/slang/{include/slang/compile.h, src/compile.cpp}` — additive, ABI-additive:
1. Per-target preprocessor macro injection (`MEL_TARGET_*`) so one source authors dual-lane.
2. Metal argument-buffer reflection: `Mel_Slang_Metal_Arg_Field[]` +
   `metal_arg_field_count` + `metal_arg_buffer` on `Mel_Slang_Reflection`, populated only for
   the MSL target when the push-constant struct lowered to a mixed argument buffer (a
   `DescriptorHandle` field present). Existing callers are unaffected (new trailing fields,
   zeroed). Both marked `MEL_FLAG(metal-bindless-reflection)` in the source.

`BindlessSpaceIndex` (design §3.3/§4) was NOT needed — the dual-lane split keeps Vulkan on
its hand-placed Binding 4 heap, so Slang's synthesized-heap descriptor-set pinning is moot.

## CLAUDE.md suggestions

None.

## Suggestions

- An arg-buffer pool (reuse by encoded length, recycled on command-buffer completion) would
  remove the per-dispatch allocation. Small, isolated; worth it once a real workload dispatches
  many bindless from_slang kernels per frame.
- Extend the dual-lane pattern to `blit.slang`/`bindless_present` so hello-gpu's on-screen
  present runs the Metal `DescriptorHandle` lane too (sampled-texture + sampler in the argument
  buffer at the fragment stage via the render encoder). The RHI render-encoder arg-buffer build
  is symmetric to the compute one already landed; the sampler resolution path is in place.
- `DescriptorHandle<ConstantBuffer<T>>` is still rejected by Slang 2026.10.2 (type mismatch),
  so uniform-buffer bindless stays on the classic path — unchanged, acceptable.

## Open questions for Gabbo

1. The per-dispatch arg-buffer allocation: pool now, or defer until a multi-dispatch bindless
   workload exists?
2. Should the render-encoder arg-buffer build (graphics from_slang bindless, e.g. a
   dual-lane `blit.slang`) land next, closing hello-gpu's Metal present on the new lane?
3. The Metal arg-buffer member-index == Slang-field-index assumption was probed on storage-image
   + scalars. Mixed buffers/samplers in one Root were not exercised end-to-end (only storage
   image is in the test). Want a multi-resource Metal bindless test before relying on it broadly?
