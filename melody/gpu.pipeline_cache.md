# gpu.pipeline_cache

GPU pipeline object cache on `Mel_Gpu_Device`. Deduplicates `VkPipeline` creation by hashing the full pipeline creation parameters. Follows Blender's VKPipelinePool pattern.

## Ownership

The cache owns all `Mel_Gpu_Pipeline` objects it creates. Callers receive pointers but must NOT destroy them. The cache destroys all pipelines on shutdown.

Lives on the device as `dev->pipeline_cache`. Initialized during device init, destroyed before VkDevice teardown.

## Cache Key

3-way split (future VK_EXT_graphics_pipeline_library ready):

- **VertexIn**: topology + vertex bindings/attributes
- **Shaders**: shader pointer + packed render state + push constants + descriptor bindings + extra set layouts
- **FragmentOut**: blend mode + color/depth formats

Each sub-key is an XXH3-64 hash. The composite key is 24 bytes (3 × u64). Lookup hashes the 24-byte key into a `Mel_HashMap`.

## Packed Pipeline State

`Mel_Gpu_Pipeline_State` is a u64 packing all render state bits: blend(4) + cull(2) + topology(3) + depth_test(1) + depth_write(1) + depth_compare(3) + pipeline_type(2) + dynamic_cull(1) + use_texture(1). Cheap to hash and compare.

## API

```c
Mel_Gpu_Pipeline* mel_gpu_pipeline_cache_get(cache, dev,
    .shader = ..., .color_format = ..., .blend_mode = ..., ...);
```

Returns cached pipeline or creates a new one via `mel_gpu_pipeline_init_opt`. Thread-safe design possible (sentinel + condvar pattern) but not implemented yet.

## Integration

The cache calls the existing `mel_gpu_pipeline_init_opt` on miss. No Vulkan-specific code — the cache is backend-agnostic. A `VkPipelineCache` object for accelerating compilation is a natural follow-up inside the Vulkan backend.
