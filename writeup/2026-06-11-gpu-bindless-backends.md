# GPU bindless — Metal and D3D12 growable rungs

Continuation of `2026-06-10-gpu-bindless-growable.md`: the Vulkan B1+B2 work was merged to main,
then the remaining backends were brought to the growable-bindless level where their floors allow.

## Work done

**Metal (§4.5, new rung — implemented and tested).**
- The tier-2 argument-buffer heap seeds small (1024 images/buffers, 256 samplers) and grows by
  buffer copy + swap; the old table stays alive for in-flight encodings via Metal's command-buffer
  retention of directly-bound buffers, so no epoch machinery is needed — the idiomatic lowering of
  the spec's future-gated pool retire.
- Reclaim: destroyed resources' side-table retains and residency-set entries now release on the
  submit-serial watermark (a small deferred list drained from `submit_complete`). Previously
  destroyed objects were retained until slot reuse — and the reuse-time release was an
  in-flight-unsafe immediate drop; both fixed.
- Residency batching: `MTLResidencySet` `commit`/`requestResidency` moved from per-registration to
  a dirty-flagged flush at queue submit / frame end (the round-3 O(N) debt).
- `caps.memory.bindless` reports `growable` + seeds. gpu-metal 13/13 including the new
  `metal_bindless.heap_grows_past_seed` (grow past seed, dispatch through a post-grow slot,
  readback); cube/texquad/plasma screens clean on `--gpu=metal`.

**D3D12 (B4 — implemented, UNTESTED: win-pilot unreachable all session).**
- B3 (slot reclaim) was found already realized — destroys future-gate via
  `table_remove_deferred` + `has_reclaim`; the round-3 "no slot reclaim" debt note was stale.
- Root signatures no longer bake heap capacities or sub-range offsets: the bindless block emits
  five descriptor tables (four resource classes + samplers), each a single unbounded
  `DESCRIPTORS_VOLATILE` range at offset 0. Registers/spaces are unchanged (SRV t0/space0,
  UAV u0/space0, CBV b1/space0, UAV u0/space1, samplers s0), so existing signed DXIL fixtures
  remain valid. Table bases are set at bind time from the device's current class bases.
- Registrations write a CPU mirror heap and copy the descriptor into the shader-visible heap
  (shader-visible heaps cannot be copy sources); grow rebuilds both heaps with per-class
  index-stable `CopyDescriptorsSimple` from the mirror and swaps. Old shader-visible heaps retire
  on COM-refcount epochs: command lists AddRef the heaps they bind, refs transfer to the
  submission at submit (`mel_gpu__defer_free_marked`), release in the deferred drain.
- Resource classes seed 1024 with a 250k/class wall (4 × 250k = the 1M shader-visible heap limit);
  samplers stay fixed at the 2048 sampler-heap wall. Registration gained the capacity gate it
  previously lacked entirely (out-of-range registration used to write past the heap).

**Misc.** Vulkan B1+B2 merged to main and pushed; module readme refreshed (stale "Metal/WebGPU not
yet built" fixed); spec realization notes updated; WebGPU remains honestly gated (no bindless
floor until M4 sized binding arrays / `GPUResourceTable`).

## Kludges

- **D3D12 work is committed untested.** win-pilot never became reachable; the code compiles on no
  local toolchain (win32-only sources). First action when the box is up:
  `ssh win-pilot "cd /d D:\repo\OrgWall && git pull --ff-only && C:\Users\Gabbo\dev.cmd nob test gpu-d3d12 win32 --gpu=d3d12"`.
  Expect debug-layer feedback on the unbounded-range root signatures and the mirror-copy path.
- **D3D12 whole-heap rebuild per class grow.** Growing any class rebuilds the entire CBV/SRV/UAV
  heap (all four classes' descriptors copied) because the classes share one shader-visible heap.
  Amortized fine (geometric), but a grow-heavy startup pays 4-way copies; pre-sizing (B5) is the fix.
- **Metal unregister reads `dev->submit_serial` without `submit_lock`** (aligned u64 load on
  Apple targets). Same convention as the marker stamping elsewhere on that backend, but it is a
  relaxed read where Vulkan takes the lock.
- **No D3D12 grow test exists yet** — `test_d3d12.c` needs a `heap_grows_past_seed` analog once
  the box is reachable.

## CLAUDE.md suggestions

- None.

## Suggestions

- Port the `post`/`gallery` hello-gpu screens off embedded SPIR-V to runtime Slang — they are the
  last two screens that cannot run on Metal (pre-existing; surfaced while verifying this work).
- B5 (`bindless_heap_create` seeds/growth_factor) is now additive across all three native backends.
- The D3D12 sampler intern + 2048 sampler-heap wall pairing should eventually get the same
  `*_make_indirect` escape the spec names for genuine wall pressure.
