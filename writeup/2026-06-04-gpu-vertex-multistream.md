# GPU multi-stream vertex buffers

## Work done

Made multi-stream vertex buffers real on every backend. Previously the core vertex
layout (`Mel_Gpu_Vertex_Element {location, format, offset}` + a single `vertex_stride`)
could not express which buffer slot an attribute reads from, so all attributes pinned
to slot 0. Metal additionally LOUD-REJECTED `cmd_bind_vertex_buffer` for any `slot != 0`.

### API delta (additive, backward-compatible)

`modules/gpu/include/gpu/pipeline.h`:

- `Mel_Gpu_Vertex_Element` gained `u32 buffer_slot`. Zero-init => slot 0 => existing
  behavior. Every legacy element literal that omits it routes to slot 0 unchanged.
- New struct `Mel_Gpu_Vertex_Buffer_Layout { u32 slot; u32 stride; bool per_instance; }`.
- `Mel_Gpu_Pipeline_Opt` gained `const Mel_Gpu_Vertex_Buffer_Layout* vertex_buffers;
  u32 vertex_buffer_count;` beside the existing `vertex_layout`/`vertex_stride`.

Contract:
- `vertex_buffer_count == 0` (the legacy/common path): backends synthesize a single
  per-vertex binding at slot 0 with `stride = vertex_stride`. Reflection-driven layouts
  (no explicit layout, no stride) are unaffected. This is the EXACT pre-existing code
  path; `vertex_stride` is still honored as slot 0's stride.
- `vertex_buffer_count > 0`: each entry declares one buffer slot (binding index = slot,
  per-slot stride, per-vertex|per-instance step). Attributes route by `buffer_slot`.
  An attribute referencing a slot with no matching `vertex_buffers` entry is rejected
  loudly at pipeline_create (MEL-ENGINE-VIII), on all four backends.

No enum was introduced for the input rate; `bool per_instance` carries the per-vertex
vs per-instance axis (avoids MEL-CODE-001 entirely). No fixed `[MEL_MAX_*]` arrays — all
per-slot tables are dynamically allocated through the device allocator (MEL-CODE-002/003).

### Backward-compat proof

Every existing pipeline-create call site (`apps/hello-gpu/src/{triangle,passthrough,
prepass,depth3d,msaa,shadow}.c`, `modules/gpu/test/test_{vulkan,metal,webgpu,scene}.c`)
sets `{location,format,offset}` + `vertex_stride` and leaves `buffer_slot`/`vertex_buffers`
zero-init. They compile and run UNCHANGED: hello-gpu links on vulkan/metal/webgpu;
gpu-vulkan 48/48, gpu-metal 6/6, gpu-webgpu 4/4, gpu-visual 13/13, gpu-scene
(vulkan/metal/webgpu) all green.

### Per-backend wiring (slot -> binding bijection)

- Vulkan (`src/vulkan/pipeline.c`): one `VkVertexInputBindingDescription` per declared
  slot (binding=slot, stride, inputRate from per_instance); `VkVertexInputAttributeDescription
  .binding = element.buffer_slot`. Legacy path still emits the single binding=0. `record.c`
  already bound at `firstBinding=slot` — unchanged.
- Metal (`src/metal/macos/{pipeline.m,record.m,mtl_backend.h}`): vertex slots map onto Metal
  buffer indices DESCENDING from the existing base 30 — `slot s -> index 30 - s` (macro
  `MEL_GPU_METAL_VERTEX_SLOT_TO_INDEX`). Slot 0 stays at index 30 (single-stream bit-for-bit
  unchanged); descending keeps every slot below Metal's 31-buffer ceiling and clear of the
  push-constant index 0. `pipeline.m` sets each attribute's `bufferIndex` and each slot's
  `MTLVertexBufferLayoutDescriptor` (stride + per-vertex|per-instance step). `record.m`:
  the `slot != 0` loud-reject is LIFTED; it now binds `setVertexBuffer:atIndex:(30-slot)`,
  with a loud range-check rejecting `slot >= 30` (would collide with push constants).
- D3D12 (`src/d3d12/{pipeline.c,record.c,d3d_backend.h}`): `D3D12_INPUT_ELEMENT_DESC.InputSlot
  = element.buffer_slot`; `InputSlotClass`/`InstanceDataStepRate` from the slot's per_instance.
  Per-slot strides live in a dense `slot_strides[max_slot+1]` table on the pipeline object;
  `record.c` IASetVertexBuffers at `StartSlot=slot` with `slot_strides[slot]`. The dxil agent's
  semantic-by-input_register mapping is preserved verbatim.
- WebGPU (`src/webgpu/pipeline.c`): one `WGPUVertexBufferLayout` per slot (arrayStride=slot
  stride, stepMode), attributes partitioned into contiguous slices per slot; `buffers[slot]`
  is that slot's layout. `record.c` already `setVertexBuffer(slot,...)` — unchanged.

### Multi-stream test

`modules/gpu/test/test_scene.c` :: `scene_shared.triangle_multistream`. Reuses the existing
cross-backend `triangle` bundle (vertex shader: pos@location0, color@location1, fragment
interpolates color). Splits the interleaved triangle into TWO buffers — positions in vertex
buffer slot 0, colors in slot 1 — one pipeline (two `vertex_buffers` entries), draws, reads
back, and diffs against the SAME `shared/triangle` golden as the single-stream test. Asserting
bit-identity to the proven interleaved result is the strongest possible check: the split
streams must interpolate to the exact same pixels.

Passes on Vulkan, Metal, and WebGPU (macOS). On D3D12 it builds and runs on win-pilot
(see report).

## Kludges

- Metal vertex-buffer index range. Single-stream historically lived at fixed index 30. To
  add slots without (a) colliding with push constants at index 0 and (b) exceeding Metal's
  31-buffer limit, I made slots DESCEND from 30 (`30 - slot`). This preserves slot 0 at 30
  (zero behavioral change for existing pipelines) and yields 30 usable slots, but it is a
  convention, not a hard cap from the API. If a future Metal pipeline needs both >29 vertex
  slots AND bind-group buffers, the index map will need rethinking. Confessed as sanctioned
  debt: today the Metal backend has no bind-group buffers (only push constants @0 + vertex
  streams), so the descending map is collision-free now.

- The D3D12 per-slot stride table is dense (`slot_strides[max_slot+1]`), so a layout that
  declares e.g. only slot 7 allocates 8 u32 slots. Sparse high slot numbers waste a few bytes.
  Acceptable: slot indices are small and contiguous in every realistic layout.

No other shortcuts. No comments added. No enums added. No fixed `[MEL_MAX_*]` arrays.

## CLAUDE.md suggestions

None.

## Suggestions

- A per-slot `offset` at bind time (`cmd_bind_vertex_buffer(cmd, slot, buf, offset)`) would
  let one buffer back multiple slots at different byte offsets (interleaved-into-split). The
  current binding always uses offset 0; the offset is a natural next axis if instancing/skinning
  apps want it. Vulkan/Metal/D3D12/WebGPU all accept a per-binding offset.
- Per-instance was wired through (`per_instance` -> INSTANCE step on all four backends) but is
  not yet exercised by a test. A small instanced-draw golden would lock it down.
