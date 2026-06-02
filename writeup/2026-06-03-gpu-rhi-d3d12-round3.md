# GPU RHI M2 — D3D12 co-primary round 3: storage-image bindless, classic descriptor sets, reflect test

Closes the three Phase-3/4 confessed gaps. Off `origin/main` (`b0ab013`, comment-free). Branch
`worktree-agent-ac4c0bc7fcc72dcaa`, pushed; `main` untouched. Only `src/d3d12/**` + `test_d3d12.c` edited; no
`include/**` change. **Zero code comments** (the module is now comment-free; global rule honored).

Built + tested on win-pilot (Win10 22H2, RTX 2060 SUPER, in-box SDK, clang/MSVC). **gpu-d3d12 16 passed / 0
failed / 1 skipped, of 17**, debug layer on with **break-on-ERROR/CORRUPTION armed** — clean run, no break, no
`leak:` lines. The 1 skip is the pre-existing `d3d12_swapchain.present_clear_readback` (DXGI swapchain
uncreatable in the SSH service window station; environmental, unchanged from Phase 4).

## Work done

### 1. Storage-image (UAV-texture) bindless class
The Phase-3 root signature reserved `base_storage_image` but had no table range. Added:
- `binding.c` `mel_gpu__bindless_register_storage_image` — typed UAV at `srv_cpu(base_storage_image + index)`,
  dimension from the view (`mel_gpu__uav_dim`). `texture_view_create` now registers it when the parent carries
  `MEL_GPU_TEXTURE_STORAGE` (symmetric to the SAMPLED→SRV path). `slot == handle.index` holds.
- `pipeline.c` bindless root sig: a **4th** CBV/SRV/UAV-table range — UAV `u0 space1`, offset
  `base_storage_image`. Distinct register space from the storage-buffer UAV (`u0 space0`) so a shader can declare
  `RWTexture2D<float4> g_images[] : register(u0, space1)` beside `RWByteAddressBuffer g_buffers[] : register(u0,
  space0)`; both ranges coexist in the one shader-visible heap.
- Test `d3d12_compute.storage_image_bindless`: a compute shader writes `float4(uv, 0.5, 1)` to a heap-addressed
  storage image, copy-to-readback, pixel-verify (`(0,0)→0,0,128,255`; `(4,4)→128,128,128,255`). Mirrors the
  Vulkan `storage_image_bindless` proof.

### 2. Classic descriptor-set path (the `bind_group` / `set_layouts` peer)
New `src/d3d12/bind_group.c` — the non-bindless root-signature-from-app-layouts path:
- `mel_gpu__classic_init/destroy` — two **shader-visible** classic heaps (CBV/SRV/UAV 4096, sampler 512) with a
  bump cursor, allocated at every device-create (so the path works on a device that requested **no**
  `descriptor_indexing`, i.e. no bindless heap — Task 2's "device with no heap").
- `bind_group_layout_create/destroy/alive` — stores the entries + a per-class descriptor count.
- `bind_group_create` — bump-allocates a contiguous resource block and a sampler block from the classic heaps.
- `write_texture` (SRV or UAV-by-kind), `write_sampler`, `write_buffer` (UAV/CBV by kind) — materialize the
  descriptor at the group's heap slot. `write_combined` **errors loudly** (D3D12 has no combined image-sampler;
  the caller must declare separate `SAMPLED_IMAGE` + `SAMPLER` bindings) — MEL-ENGINE-VIII, no silent default.
- `cmd_bind_descriptor_set` — binds the two classic heaps once per list, then sets the set's resource/sampler
  root descriptor tables from the bound pipeline's per-set param map.
- `pipeline.c` `build_root_sig` now builds **per-set descriptor tables** from `set_layouts` (register space = set
  index; resource bindings in one table, samplers in a sibling table since D3D12 forbids mixing). Records a
  `Mel_Gpu_Set_Param` per set (resource-table param, sampler-table param). `set_layouts` no longer returns
  `MissingFeature`.
- `record.c` — `cmd_bind_pipeline` stashes `cur_pipeline`; `command_list_begin` resets it + the
  `classic_heaps_bound` latch.
- Test `d3d12_bind_group.classic_descriptor_set`: on a **no-bindless** device, a fullscreen triangle samples an
  app-owned descriptor table (set 0: `SAMPLED_IMAGE` t0 + `SAMPLER` s0), render→readback, pixel-verify
  (`70,140,210,255`). Mirrors the Vulkan `classic_descriptor_set` proof.

### 3. `reflect.c` input-signature unit test
- `reflect.c` `mel_gpu__dxil_reflect_test` — a test hook that runs the ISG1/ISGN reader and copies the derived
  layout (semantic / index / format / offset / stride) into caller arrays (avoids exposing the internal
  `Mel_Gpu_Dxil_Input` struct; declared `extern` in the test, same pattern as the swapchain hooks).
- Test `d3d12_reflect.input_signature`: feeds a known VS DXIL (`POSITION` float3, `COLOR0` float4, `TEXCOORD0`
  float2) and asserts semantic + index + format (`RGB32/RGBA32/RG32_FLOAT`) + tight-packed offset (0/12/28) +
  stride (36). This is the first test to actually exercise the ISG reader (Phase-3 shaders had no vertex input).

## Verification
`ssh win-pilot … nob test gpu-d3d12 win32 --gpu=d3d12` — **16 passed, 0 failed, 1 skipped, of 17**,
break-on-ERROR/CORRUPTION armed, no `leak:` lines. The three new tests ran green; the 13 prior stayed green (the
4-range bindless table + always-on classic heaps did not regress the sample/compute paths). win-pilot left on
`main`.

## What is proven vs deferred
- **Proven on hardware:** storage-image UAV bindless write→readback pixel; classic app-owned descriptor-table
  render→readback pixel on a heapless device; the ISG reader against a real 3-input vertex DXIL; debug layer
  clean across all of it.
- **Deferred:** the swapchain present path still skips over SSH (unchanged environmental limit). Agility-SDK
  ceiling (`ResourceDescriptorHeap` direct indexing, enhanced barriers, GPU upload heaps) still deferred.

## Kludges and debt (confessed, MEL-ENGINE-VIII)
- **Classic heaps are fixed-size bump allocators (4096 res / 512 smp), never grown or reclaimed.** A bind-group
  destroy frees its obj but does **not** reclaim its heap slots — a create/destroy churn leaks heap space until
  device destroy. Exhaustion errors loudly (no silent overrun) but a real app wants a free-list or generational
  ring. The fixed caps also brush MEL-CODE-002; a growable classic heap (realloc + re-register, or a chained
  heap) is the principled form. Sanctioned for the proof; flagged.
- **No per-CL state tracking for classic descriptors.** `cmd_bind_descriptor_set` trusts the caller wrote every
  binding before binding the group; an unwritten slot is GPU-UB, not a create-time error (symmetric to the
  bindless over-index note).
- **`write_combined` is a hard error on D3D12**, not a silent split. Correct per the API contract, but a portable
  app that uses `COMBINED_IMAGE_SAMPLER` on Vulkan must branch for D3D12. A future convenience could auto-split a
  combined binding into adjacent SRV+sampler ranges; not done.
- **Storage-image UAV/SRV writes always fill the `Texture2D` union member** (classic `write_texture` and the
  bindless registrar). Non-2D storage views (3D/array/cube) would mis-fill; the tests use 2D only. Same
  Phase-3-carried limit as the SRV path.
- **`set_params`/`ranges`/`params` are heap-allocated per pipeline-create via the device allocator** (MEL-CODE-003
  honored), `params`/`ranges` freed after serialize, `set_params` lives with the pipeline. Correct, but one
  allocation-pair per create; a scratch arena would be tidier.
- **The reflect test hook copies into a `char[32]` semantic buffer** (truncates at 30). Fine for the known
  semantics; a longer semantic would silently truncate in the *test* (not the production reader, which dups the
  full name).
- **`mel_gpu_bind_group_layout`/`bind_group` leaks at device-destroy leak their `entries`/obj memory too** (the
  leak path only reports, doesn't free), since a leak is already an error the test never trips. Matches the
  Vulkan posture.
- **clang-format not run** (absent on the dev mac, MEL-CODE-004); house style matched by hand.

### Rule-#1 / enum carve-outs
- **Comments:** none written — the module is comment-free and the global rule governs this slice (the round-3
  HARD RULE confirmed it). Zero comments in every touched file (verified by grep).
- **Enums:** reuses the public `Mel_Gpu_Descriptor_Kind` and maps onto `D3D12_DESCRIPTOR_RANGE_TYPE_*` /
  `DXGI_FORMAT` / `D3D12_UAV_DIMENSION_*` (the protocol-mapping carve-out, MEL-CODE-001).

## For Gabbo / coordination
- **No `include/**` change taken.** The classic path uses the existing public `bind_group.h` surface unchanged;
  the storage-image registrar is internal. Nothing flagged for the Vulkan teammate.
- The classic-heap caps + no-reclaim are the first thing to revisit if any app drives many bind groups; a
  growable heap is the right next step (would also retire the MEL-CODE-002 brush).
- `modules/gpu/readme.md` still absent (flagged in every prior writeup). The D3D12 binding conventions now have a
  second axis worth recording there: classic = app descriptor tables over per-device shader-visible heaps
  (register space = set index, separate sampler table), `write_combined` unsupported; bindless storage-image =
  `u0 space1` in the one heap.
