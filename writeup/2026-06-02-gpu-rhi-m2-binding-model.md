# GPU RHI M2 — the binding model (U11 samplers, U14 bindless heap + BDA ceiling, U12 reflection-lite, Vulkan)

Continues the GPU RHI rewrite (`design/gpu-rhi.md`) where M2 Phase A stopped
(`writeup/2026-06-02-gpu-rhi-m2-phase-a.md`). Phase A delivered the recording/state/rendering backbone but
left pipelines with empty descriptor layouts — a shader could draw vertex-coloured geometry but could not
read a buffer or sample a texture. This session lands the **binding model**, both layers: U11 samplers, the
U14 bindless descriptor heap **floor** and the buffer-device-address **ceiling**, U12 reflection-lite, and
future-gated bindless slot reclamation. Headless render tests prove a shader sampling a heap-resident
texture through a heap-resident sampler, and a shader dereferencing a buffer purely by GPU pointer.

The runnable half is Vulkan/macOS over MoltenVK, which on the M3 Pro grants the full descriptor-indexing
floor (runtime arrays + update-after-bind + non-uniform indexing + partially-bound) **and** buffer device
address by default. The descriptor-indexing floor is the *earlier Vulkan ceiling* and is reported as
`bindless.tier = full` per §6.7; the `descriptor_buffer` / `descriptor_heap` path is a later additive
lowering of the same surface.

## Work done

- **U11 samplers** (`gpu/sampler.h`, `src/vulkan/sampler.c`). Value handle, slotmap, leak-reported. Filter /
  wrap / anisotropy (clamped to the device limit; `samplerAnisotropy` enabled when present) / compare /
  border / lod. **Auto-dedup** by canonical-key FNV-1a hash with collision-safe `memcmp`, reference-counted:
  identical descriptors share one handle and one `VkSampler`; the slot retires at refcount 0.
- **U14 bindless heap — floor** (`gpu/binding.h`, `src/vulkan/binding.c`). One device-global descriptor set,
  a large partially-bound update-after-bind array per class (sampled image / sampler / storage buffer /
  uniform buffer / storage image). Resource creation auto-registers the descriptor at the resource's slotmap
  handle index — the **slot == handle.index** direct contract (§3.1), which holds because `handle.index` is
  a stable sparse-slot id. `caps.memory.bindless` became a struct (tier + per-class limits) refined to the
  realized heap capacity.
- **U14 — pointer-bearing ceiling (BDA)** (`buffer.c`, `memory.c`, `binding.h`). With
  `buffer_device_address` granted, every memory block is allocated `DEVICE_ADDRESS`-capable;
  `MEL_GPU_BUFFER_DEVICE_ADDRESS` buffers expose a stable GPU address via
  `mel_gpu_buffer_device_address`. A pipeline can then carry that address in its push-constant root record
  and the shader dereferences the buffer's data directly — no descriptor set involved. `shaderInt64` is
  enabled alongside BDA (buffer-reference pointer arithmetic declares the Int64 SPIR-V capability).
- **Binding-model introspection.** `caps.memory.bindless.binding_model` (root_record | descriptor_tables),
  `root_record_payload` (pointers | descriptor_indices | **mixed** — textures/samplers as indices, buffers
  as pointers), and `root_record_update` (persistent_map | …) now report which model the device's active
  configuration presents (§6.7), so a power user can branch.
- **Root-record carrier.** On the floor the per-draw record rides the reflected push-constant range; a
  bindless pipeline puts the heap at set 0 and `cmd_bind_pipeline` auto-binds it. `mel_gpu_cmd_bind_bindless`
  is the explicit P2 peer.
- **Pipeline integration.** `bindless` flag → set 0 = heap; immutable `static_samplers[]` baked into a
  dedicated set; `MissingFeature` vs `MissingBindlessSlot` statuses (see debt — the latter is still
  unreachable).
- **U12 reflection-lite** (`src/vulkan/reflect.c`). A single-pass SPIR-V reader extracts the push-constant
  block size (resolving struct member offsets + scalar/vector/matrix/array/struct member sizes) and
  descriptor-set-0 usage. `pipeline_create` derives the push-constant size and the bindless flag from
  reflection — the shader is the source of truth (§6.4) — with the explicit pipeline-opt fields as override.
  Cross-checked against `spirv-dis`.
- **Future-gated bindless slot reclamation** (the correctness fix from the prior review). Added a two-phase
  removal to the slotmap (`collection`): `remove_deferred` marks the slot dead and rolls its generation
  immediately (use-after-free stays a loud failure) and swap-removes the dense payload, but **withholds the
  index from the free list**; `reclaim` returns it once the caller's retirement condition is met.
  Buffer / texture-view / sampler destroys route through deferred reclaim **piggybacked on the existing
  retirement watermark**, so a heap slot index is never reused while an in-flight submission still reads it.
  The heap descriptor at a reused slot is overwritten by the new resource, never left stale.

## Verification

- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan` —
  **18/18** (the Phase-A twelve plus six `vk_bindless`): `sampler_dedup`, `sample_texture_readback`
  (a fullscreen triangle samples a heap-resident texture through a heap-resident sampler, layout derived
  **purely by reflection**, pixel-verified), `static_sampler_pipeline_create`, `missing_feature_without_heap`,
  `slot_reuse_samples_correct` (destroy a heap-registered view, recreate at the reclaimed slot —
  `slotB == slotA` — and confirm sampling returns the **new** resource's colour), and
  `bda_pointer_root_record` (a shader dereferences a buffer purely by its device address, no descriptors,
  pixel-verified; caps assert `binding_model = root_record`, `payload = mixed`).
- `./nob test collection-slotmap` — **3/3** (new). The deterministic gating invariant: `remove_deferred` →
  `get` NULL / `alive` false / generation rolled / index **not** reused by the next insert until `reclaim`,
  then reused; live and double reclaims rejected; survivors readable after a deferred interior removal.
- `./nob test gpu-foundation` — **8/8** (host). `./nob build hello-gpu macos --gpu=vulkan` builds; the cube
  runs for seconds with no validation errors (the existing pipelines pass clean through the now
  reflection-aware `pipeline_create`).
- **Zero validation errors, zero leaks, zero VUIDs** across the GPU suite. The one logged `ERROR` is the
  intentional `MissingFeature` diagnostic emitted at the failure site by the negative test (MEL-ENGINE-VIII).

## Kludges and debt (confessed, MEL-ENGINE-VIII)

The prior review's headline gap — **bindless slot reclamation not future-gated — is now FIXED** (above) and
covered by both a slotmap-level deterministic test and a GPU-level reused-slot render test. What remains:

- **`MissingBindlessSlot` is still dead code.** The status value is defined and distinguished from
  `MissingFeature`, but reflection-lite does not extract per-binding array bounds, so the engine cannot yet
  detect "shader demands heap slot N ≥ heap size." Only `MissingFeature` is reachable. (Closing this needs
  reflection to read descriptor-array sizes.)
- **No `melody.binding` Slang mixin.** The named M2 deliverable — one resource-set declaration synthesizing
  both the pointer (ceiling) and index (floor) forms — is absent; shaders are hand-authored GLSL with literal
  `set`/`binding` and explicit `buffer_reference`. The duality is demonstrated by two separate shaders, not
  one authored declaration. Needs the vendored-Slang / codegen path.
- **Reflection-lite remains lite.** It extracts push-constant size + set-0 usage only — not vertex input,
  per-binding descriptor types, or specialization constants (the latter is a named §6.4 deliverable, absent).
  It assumes the engine convention that **descriptor set 0 is the bindless heap** (any set-0 use ⇒ bindless),
  which **forecloses the classic descriptor-set P2 path** (`cmd_bind_descriptor_set` / app-owned set 0) until
  reflection distinguishes. 64-bit array-length constants and multiple disjoint push-constant blocks aren't
  handled (the latter takes a max-size union).
- **Named M2 items still skipped:** mutable descriptor types (`Mel_Gpu_Mutable_Descriptor_Layout`,
  `caps.memory.mutable_descriptor_type`); `CAPTURE_REPLAY` wired through heap creation; YCbCr conversion
  sampler reference (§6.3); a public reflection query surface (reflection is engine-internal).
- **Sampler / wrap / filter / compare / border are enums** (MEL-CODE-001) — same protocol-mapping carve-out
  and Rule-#1 flag as the format/state enums. **`Mel_Gpu_Binding_Model` / `Root_Record_Payload` /
  `Root_Record_Update` are also enums** — protocol-tier reporting, same carve-out.
- **Heap caps are fixed defaults** (16384 / 2048 / 16384) clamped to the device limit, not grown on demand;
  over-cap asserts `BindlessSlotExhausted`. The indirect families (`*_Indirect`) are typedef'd with no
  create/resolve path (capped/compacted bindless deferred). No `texture_view_create_indirect` resize path.
- **Static / immutable samplers: lifetime coupling is the caller's contract**, not engine-enforced (no
  refcount claim held for the pipeline's lifetime); only pipeline-create is tested, not shader-side use.
- **`cmd_bind_pipeline` re-binds the heap set on every bindless bind**, not once per command list.
- **No storage-buffer / uniform-buffer / storage-image pixel test** (needs a compute pipeline — U13). The
  heap + bind use the **graphics** bind point only. Sampler intern lookup is a **linear scan**. No
  **concurrency test** for the U36 classes (concurrent sampler_create, per-region heap-write serialization).
  No `caps.sampler` domain (anisotropy clamped via a device-stored limit). `caps.memory.bindless` changed
  from a flat enum to a struct (only the caps probe consumed the old form).
- **BDA scope.** The pointer payload is proven for a buffer-reference *read*; the engine does not yet provide
  the `melody.binding` `RootRecord<T>` carrier or GPU-generated root records (`root_record_update =
  gpu_generated`), and D3D12 (where buffers are descriptor indices even on the ceiling) is not buildable here.

## CLAUDE.md / repo-convention suggestions (recommendations only)

- The `size`-typedef trap did **not** bite — byte counts were named `bytes` / `range` / `size_bytes` /
  `size_bytes`. The standing recommendation (hygienic `countof` / rename the `isize` typedef) holds.
- `./nob` is per-worktree; bootstrap with `clang -std=c23 -g -Imodules/build -o nob nob.c` before any build
  in a fresh worktree.
- The two-phase slotmap removal (`remove_deferred` / `reclaim`) is a general primitive; §3.3 lists four
  future-gated reclamations (command-pool reset, transient-ring reset, deferred-destroy, bindless slot). The
  command-pool and transient-ring resets should adopt the same primitive rather than re-deriving gating.
- `modules/gpu/readme.md` still absent (Phase A suggested it). Heap conventions (set 0 = bindless,
  slot == handle index, BDA = pointer payload) and the reflection-derived-layout default belong there.

## Suggestions

- Next: teach reflection-lite to read descriptor-array bounds (unlocks the real `MissingBindlessSlot`) and
  vertex-input layout, then the `melody.binding` Slang mixin so one declaration emits both payload forms.
- Storage-buffer bindless and GPU-generated root records both want U13 compute as their first consumer.

## Shader sources (for `modules/gpu/test/bindless_spv.h` regeneration)

`bindless.vert` (fullscreen triangle), `bindless.frag` (heap sample), `solid.frag` (no-descriptor probe),
`bda.frag` (pointer dereference). Regenerate with `glslc -fshader-stage={vert,frag} -mfmt=c <src>` then wrap
as `static const uint32_t NAME_SPV[] = { … };`.

```glsl
// bindless.vert
#version 460
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
```
```glsl
// bindless.frag  (descriptor-index floor)
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 1) uniform sampler   u_samplers[];
layout(push_constant) uniform Root { uint tex; uint smp; } root;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { o_color = texture(sampler2D(u_textures[root.tex], u_samplers[root.smp]), v_uv); }
```
```glsl
// solid.frag  (no descriptor arrays — probes MissingFeature without RuntimeDescriptorArray)
#version 460
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { o_color = vec4(v_uv, 0.0, 1.0); }
```
```glsl
// bda.frag  (pointer-bearing ceiling — dereference a buffer by device address, no descriptors)
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer Color { vec4 rgba; };
layout(push_constant) uniform Root { uint64_t addr; } root;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { Color c = Color(root.addr); o_color = c.rgba; }
```
