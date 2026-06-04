# Metal bindless path — argument-buffer tier 2 device-global heap (task #34)

## Work done — what changed, and why

The Metal backend reported `caps.bindless = none` and loud-stubbed bindless pipelines, sampler/buffer/texture-view bindless-slot queries, and `cmd_bind_bindless`. This gated the hello-gpu compute/graphics screens (which bind storage/textures via the device-global bindless heap) on Metal. Implemented the full bindless path; gate is now lifted for the realized surface.

### Design — argument-buffer tier 2 heap

`design/gpu-rhi.md` §6.7 names the Metal ceiling as `MTL4ArgumentTable` + integrated `MTLResidencySet`. This round lowers to the Metal 3-era equivalent that the M3 Pro (and every Apple-Silicon device) honors: **argument-buffers tier 2 direct-write heaps + `MTLResidencySet`** — the same shape, one generation below the MTL4 explicit argument table.

- **Five per-class heaps** (mirroring the Vulkan reference's five descriptor-set bindings): sampled images, samplers, storage buffers, uniform buffers, storage images. Each heap is a `MTLResourceStorageModeShared` `MTLBuffer` sized `cap * 8` bytes. With tier 2, a heap slot is written directly by the CPU: textures/samplers store an 8-byte `MTLResourceID` (`gpuResourceID`); storage/uniform buffers store an 8-byte `MTLGPUAddress` (`gpuAddress`). Uniform 8-byte stride across all five classes.
- **`slot == handle.index` registration (§3.1 contract).** Engine-created direct resources auto-register at their slotmap index at creation time: `buffer_create` (storage/uniform), `texture_view_create` (sampled/storage from the source texture's usage), `sampler_create`. Registration writes the resource's id/address into `heap[index]`. Over-cap creation refuses with `..._CREATE_BINDLESS_SLOT_EXHAUSTED` (the fixed-capacity-heap contract). Samplers set `supportArgumentBuffers = YES` on the descriptor when bindless is enabled (required for `gpuResourceID` validity).
- **Residency via `MTLResidencySet`.** One device-global set, `addResidencySet` to the queue once at heap init. Each registration `addAllocation` + `commit` + `requestResidency`, so every bindless resource is GPU-accessible for both render and compute encoders on the queue without per-encoder `useResource`. Released via `removeResidencySet` at shutdown.

### Per-draw / dispatch root record (carrier)

The five heaps bind at fixed buffer indices `1..5` (`MEL_GPU_METAL_BINDLESS_HEAP_INDEX`), clear of the push-constant root record at index 0 (params@0) and the descending vertex-buffer indices (30-down). Bindless pipelines and the classic vertex/manual-storage path are mutually exclusive per pipeline (matching Vulkan set-0-vs-classic), so heaps 1..5 never collide. `cmd_bind_bindless` binds all five heaps on whichever encoder is active (render: vertex+fragment; compute: kernel). The push-constant root record (heap indices) reaches the shader through the existing `cmd_push_constants` at index 0. Works on **both** the render encoder and the compute encoder.

### Caps tier reported

`caps.memory.bindless.tier = full` when `argumentBuffersSupport >= MTLArgumentBuffersTier2` (and macOS 15 / iOS 18 for `MTLResidencySet`); `binding_model = root_record`; `root_record_payload = mixed` (buffers are real GPU addresses, textures/samplers are resource ids); `root_record_update = persistent_map` (shared heap buffers are CPU-mapped). Honest: tier-1 devices report `none` and the heap refuses to initialize loudly (no over-report). Slot maxima derived from `maxArgumentBufferSamplerCount` (samplers) and a 2^20 ceiling (resources), clamped at device-create to the default heap sizes (16384 images, 2048 samplers, 16384 buffers).

### Proof (test_metal.c, +4 tests, 8/8 -> 12/12)

- `metal_bindless.compute_storage_buffer` — **the key compute test.** 4 storage buffers registered in the heap, a compute kernel indexes them by their bindless slot (from the root record) and writes `(k+1)*1000 + gid*3 + 7`; exact-value readback on all 4. Mirrors the Vulkan gpu-visual storage-bindless semantics.
- `metal_bindless.sample_texture_readback` — graphics bindless. Source texture cleared to (0.2,0.4,0.6) via a render pass, then a full-screen bindless draw samples it through the texture+sampler heaps indexed by slot; center readback = (51,102,153). PPM dumped.
- `metal_bindless.heap_caps_reported` — asserts tier=full, binding_model=root_record, payload=mixed, nonzero slot maxima.
- `metal_bindless.missing_feature_without_heap` — a bindless pipeline on a non-bindless device fails loudly with `MISSING_FEATURE` (the lifted reject still fires when bindless wasn't requested).

### Green-run counts
- gpu-metal: **12/12** (was 8/8; +4 bindless, no regression).
- gpu-foundation: 13/13. gpu-resources: 4/4.

### Lifetime / residency notes
- Heaps + residency set retained on the device; freed at `device_destroy` before slotmap teardown.
- Resources stay resident from registration until destroy. No deferred slot reclaim (the heap is fixed-capacity; matches the Vulkan floor — `design/gpu-bindless-growable.md` is the grow spec).
- `mel_gpu_buffer_destroy` / `texture_view_destroy` / `sampler_destroy` do **not** clear the heap slot or `removeAllocation` from the residency set (debt below).

## Kludges / debt (MEL-ENGINE-VIII: confess all)

1. **Per-registration `commit` + `requestResidency`.** Each resource registration commits the residency set and requests residency individually — O(N) Metal calls under resource churn (MEL-ENGINE-III: visible cost). Correct but not batched. A batched/lazy commit (commit once per submit, or dirty-flag) is the right shape; deferred.
2. **No heap-slot reclaim on destroy.** Destroying a bindless resource leaves a stale id/address in the heap slot and the allocation in the residency set. The slotmap will reuse the index and overwrite the slot on next create, so it is not a correctness bug for the reuse path, but a destroyed-and-not-recreated slot keeps a dangling allocation resident (memory held, not use-after-free — the MTLBuffer/texture is released, but the residency set still references the old allocation object until overwritten). Mirrors the known Vulkan/D3D12 "no slot reclaim" debt. Should `removeAllocation` on destroy and clear the slot; deferred to align with the future-gated reclaim in `design/gpu-bindless-growable.md`.
3. **`buffer_device_address` still loud-stubbed.** BDA (real shader-dereferenced addresses to a user struct, the pointer-bearing root record) is target-state; the heap uses `gpuAddress` internally for buffer slots but does not expose it through `mel_gpu_buffer_device_address`. Honest stub retained.
4. **MTL4ArgumentTable not used.** The §6.7 ceiling names `MTL4ArgumentTable`. This round uses tier-2 direct-write argument buffers (the Metal 3 equivalent), bound via `setVertexBuffer`/`setFragmentBuffer`/`setBuffer`. Faithful and works on M1+; the MTL4 explicit-table lowering is a future refinement, not a behavior gap.
5. **`residency_available` availability guard is always-true at the current deployment target** (clang defaults to the host SDK's min-version = macOS 26). It is kept because it honestly documents the macOS-15 requirement and gates correctly if a lower deployment target is ever set. Not dead code — a guard for a lower floor.
6. **Heap caps are a five-element protocol set.** `heaps[MEL_GPU_BINDLESS_BINDING_COUNT]` is a fixed array of five — this is the closed descriptor-class protocol (MEL-CODE-001's sanctioned case, exactly as the Vulkan reference's `bindings[BINDLESS_BINDING_COUNT]`), not a `MEL_MAX_*` capacity bound (per-class slot capacity is dynamic in the MTLBuffer sizes). No MEL-CODE-002 violation.
7. **AS heap class absent.** Acceleration-structure bindless gates with ray tracing (M3); not in this round, consistent with Vulkan.

## What's still stubbed (loud)
- `buffer_device_address` (BDA pointer carrier) — loud error.
- `bind_group` / `bind_group_layout` / `cmd_bind_descriptor_set` (the classic P2 peer) — unchanged loud stubs (not part of this task; Vulkan has it, Metal does not yet).
- sync primitives, query pools, buffer/texture import — unchanged loud stubs.

## CLAUDE.md suggestions
None.

## Suggestions
- Wire the destroy path to `removeAllocation` + slot-clear and batch the residency `commit` to one-per-submit; both ride the future-gated reclaim in `design/gpu-bindless-growable.md`.
- A shared MSL bindless-heap prelude (the `BufSlot`/`TexSlot`/`SmpSlot` argument-buffer structs at the fixed indices) would let the hello-gpu Metal screens author one set-0 ABI instead of inlining it per shader — the Slang lane should emit this layout for Metal.

## Open questions for Gabbo
- Should the Metal bindless heap advance to `MTL4ArgumentTable` (the §6.7 named ceiling) now, or is the tier-2 direct-write lowering the intended M1-floor-compatible path with MTL4 as a later additive refinement?
- The heap binds at buffer indices 1..5. If a future bindless pipeline ever also needs manual vertex buffers, that range conflicts. Pin a reserved index policy (e.g. heaps at the top of the buffer-index space, vertex buffers below) or keep bindless-and-classic strictly exclusive per pipeline?
