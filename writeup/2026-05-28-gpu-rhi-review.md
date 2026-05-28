# GPU RHI Review

## Work done

Reviewed `docs/gpu-rhi.md` against the repo commandments, current `modules/gpu` shape, and primary vendor/spec sources for Vulkan, D3D12, Metal, and WebGPU.

Updated `docs/gpu-rhi.md` with the review fixes: queue priority now belongs to the device-create queue plan, buffer create/write/map APIs carry async/status/ordering contracts, texture resources and views are modeled separately, sampler dedup/static-sampler lowering has lifetime and WebGPU behavior, bundle capability checks no longer contradict per-entry support, D3D12 bindless uses descriptor indices rather than arbitrary GPU pointers, subgroup size is no longer a command dynamic state, state-resync is per subresource, WebGPU external-texture lifetime is source-specific, and Vulkan present timing names `VK_EXT_present_timing`.

## Kludges

None.

## CLAUDE.md suggestions

None.

## Suggestions

After the next RHI design pass, cross-check the remaining frontier API names that are still moving quickly (`VK_EXT_descriptor_heap`, Metal 4, WebGPU `GPUResourceTable`) before implementation freezes.
