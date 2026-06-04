# Slang first-class bindless on Metal — research + design checkpoint (task #37)

## Work done — what changed, and why

Goal: break the wall that gates every heap-indexing screen (mandelbrot/bloom/boids/
bindless_present) on Metal — Slang auto-emits the unbounded heap `RWTexture2D u_images[]`
as an MSL flexible-array-member kernel param that Metal's runtime compiler rejects.

This pass is **research + design only**, ending at the checkpoint the task defines. No RHI
or shader code changed. The deliverable is `design/gpu-slang-bindless.md` plus the
empirical lowering proofs below. The reason: the only Metal-viable Slang bindless
mechanism forces a coupled, cross-backend binding-model rewrite that would break the green
Vulkan path if done partially. That is the task's explicit stop condition.

### What was determined (empirically, against vendored libslang 2026.10.2 + `xcrun metal 32023.830`)

I built standalone probes linking the repo's prebuilt libslang and ran every emitted MSL
through `xcrun -sdk macosx metal -c`. Findings:

1. **The wall reproduces exactly.** `RWTexture2D u_images[]` → MSL
   `texture2d<…> u_images_0[]` → `xcrun metal` error "flexible array members are a C99
   feature". Confirmed.

2. **`DescriptorHandle<T>` as a struct field is the fix at the authoring layer.** A
   `DescriptorHandle<RWTexture2D<float4>> image` field in the push-constant `Root`
   compiles cleanly to MSL (produced a `.air`), and emits valid SPIR-V and WGSL from the
   SAME source. The re-authored mandelbrot kernel was verified end-to-end through the
   compiler for all three targets.

3. **Metal's `DescriptorHandle` lowering has NO global heap.** The handle becomes the
   resolved resource **inlined as an element of the argument buffer** that carries the
   `Root` struct (one mixed argument buffer at `[[buffer(0)]]`: the texture at arg-buffer
   id 0, plus 28 bytes of inline uniforms at reflected offsets). The host must build this
   argument buffer per dispatch.

4. **Alternatives are dead.** `ResourceDescriptorHeap` is undefined in this build.
   In-shader handle construction from a uint is rejected on Metal
   (`DescriptorHandle.init` unavailable). A `getDescriptorFromHandle` override does not
   relocate the synthesized heap on SPIR-V and is ignored on Metal. A
   `ParameterBlock<{ RWTexture2D images[] }>` global heap — the form that WOULD match the
   engine's bind-once model — also lowers to a flexible-array-member and is rejected by
   `xcrun metal`. So Metal Slang bindless is exclusively the inlined-argument-buffer model.

5. **SPIR-V/Vulkan layout mismatch.** Slang's synthesized heap lands at DescriptorSet 0,
   Binding 2 (all non-sampler resources) / Binding 0 (samplers). The engine's working
   Vulkan heap uses 5 class-bindings (storage-image = Binding 4); the current green
   mandelbrot hand-places `[[vk::binding(4,0)]]` to match. `DescriptorHandle` fields
   cannot carry `[[vk::binding]]`, and `BindlessSpaceIndex` moves only the set, not the
   binding. Adopting `DescriptorHandle` thus forces a Vulkan/WGSL heap re-layout.

### Why a checkpoint (the coupling)

The Metal fix needs the consumer ABI change (`root.image`: `uint` → `DescriptorHandle`
field), which changes the SAME shader's Vulkan lowering (heap moves to Binding 2), which
breaks the green Vulkan heap unless `binding.c` is re-laid-out to Slang's {0,2} scheme.
The three are coupled; a Metal-only change would author a shader that no longer matches
the Vulkan heap. Full plan in `design/gpu-slang-bindless.md` §3.

## Kludges / debt (MEL-ENGINE-VIII: confess all)

- **None introduced** — no code changed. The pre-existing debt this would clear is the
  `MEL_SKIP` in `modules/gpu/test/test_scene.c` (mandelbrot on Metal), still skipped with
  its honest cause. The skip's stated cause is now fully characterized by the design doc.
- **Scratch probes** live under `/tmp/slangprobe/` (not in the repo). Disposable.

## CLAUDE.md suggestions

None.

## Suggestions

- The implementation is a milestone, not a pass. Sequence it: (1) wrapper additions
  (`BindlessSpaceIndex` option + Metal argument-buffer reflection in `compile.h`), kept
  ABI-additive; (2) Vulkan/WGSL heap re-layout to Slang's {sampler@0, resource@2} with the
  suite kept green; (3) Metal argument-buffer-per-dispatch; (4) flip the shaders to
  `DescriptorHandle` and re-enable the Metal mandelbrot test. Each step verifiable
  independently except (4), which needs (1)–(3).
- Worth confirming with upstream Slang whether a future `MetalArgumentBufferTier2` layout
  rule or `-emit-spirv-via-glsl`-style flag will ever emit an indexable Metal global heap;
  if so, the engine could keep its bind-once model and avoid the per-dispatch arg buffer.

## Open questions for Gabbo

1. The required alignment is a cross-backend binding-model rewrite (Vulkan heap re-layout
   + Metal argument-buffer-per-dispatch + consumer ABI). It WILL touch the green
   gpu-vulkan 48/48 path. Approve the four-step milestone, or prefer a different shape
   (e.g. keep two authoring lanes — hand-placed unbounded array for Vulkan/WGSL, a
   `DescriptorHandle` lane for Metal — at the cost of dual-source shaders)?
2. Metal's Slang bindless fuses push-constants and bindless into one per-dispatch argument
   buffer. That contradicts the §6.7 bind-once device-global heap and the existing
   `cmd_bind_bindless`. Should the Metal Slang path own a distinct
   "argument-buffer-per-dispatch" binding model peer to the manual heap (which test_metal
   keeps using), or replace the heap entirely?
3. Slang rejects `DescriptorHandle<ConstantBuffer<T>>` (type mismatch on assignment).
   Uniform-buffer bindless via `DescriptorHandle` is therefore unavailable in 2026.10.2 —
   that class would stay on the classic descriptor path. Acceptable, or block on it?
