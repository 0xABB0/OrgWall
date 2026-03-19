# texture.pool

Engine-managed texture loading pool with integrated bindless texture table.

## Engine-Managed Texture Table

The texture pool creates and owns the global `Mel_Texture_Table` (bindless descriptor set). Initialized during the gpu_ready boot cascade. A white texture is added at index 0.

Textures are registered via `mel_texture_pool_add_to_table(Mel_Gpu_Texture* tex)`. Returns a u32 index for use in shaders as `texture_idx`. Deduplicated by image view pointer — same texture returns the same index on repeat calls.

The pipeline accesses the table via `mel_texture_pool_get_table()`. No user-facing texture table creation needed.

## Boot Flow

1. Constructor: init `mel_texture_pool_ready` event channel, register wire
2. Wire: subscribe to `mel_gpu_device_ready` and `mel_shutdown_begin`
3. gpu_ready: init texture pool, create texture table (cap=1024), add white texture, fire `mel_texture_pool_ready`
4. Shutdown: free dedup hashmap, destroy texture table, shutdown pool

Font modules subscribe to `mel_texture_pool_ready` to receive the device reference. Font textures are added to the table during `mel_ctext_*` helper calls (deduplication ensures no waste).
