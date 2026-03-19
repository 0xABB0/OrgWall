# render.pipeline.2d

Default 2D rendering pipeline. Draws all objects in the manager grouped by material, using the pipeline cache for GPU pipeline management.

## Multi-Material Drawing

The pipeline does not compile shaders or create GPU pipelines directly. Materials bring their own shaders (via `Mel_Material_Base`). At draw time, for each material group in the manager:

1. Get the material base → get its shader
2. Build a `Mel_Gpu_Pipeline_Opt` from the shader + 2D render state + target format + descriptor layout
3. `mel_gpu_pipeline_cache_get` → returns cached or creates new
4. Bind pipeline, allocate/write descriptors, push constants, draw

Descriptor layout varies by material:
- `param_size == 0` (sprites): 3 bindings — transforms, infos, draw_order
- `param_size > 0` (text): 4 bindings — transforms, infos, material_params, draw_order

Set 1 is always the bindless texture table.

## Depth Buffer

Uses a per-view depth image (`D32_SFLOAT`) with `MEL_GPU_COMPARE_GREATER` and `clear_depth = 0.0`. This is necessary because `mel_mat4_ortho` maps near→z_ndc=1.0 and far→z_ndc=0.0 (inverted from Vulkan's convention). Closer objects have higher z_ndc values, so GREATER compare puts them in front.

Sprites default to `depth = 0.5` (farther back), text glyphs to `depth = 0.0` (closer). The depth test ensures correct layering regardless of material group draw order.

## Push Constants

```c
typedef struct {
    Mel_Mat4 projection;   // 64 bytes
    u32 draw_offset;       // offset into draw_order buffer for this group
    u32 _pad[3];
} Mel_Sprite2D_Push_Constants;  // 80 bytes
```

`draw_offset` is needed because `SV_InstanceID` in Slang maps to `gl_InstanceID` (not `gl_InstanceIndex`) — it does NOT include Vulkan's `firstInstance`. Each group passes its `range.start` as the draw_offset.

## Boot Flow

Minimal: on gpu_ready, stores device ref, registers pipeline type, fires `mel_pipeline_2d_ready`. No shader compilation — materials handle that independently.

## Texture Table

Uses the engine-managed texture table from `mel_texture_pool_get_table()`. No user-facing `set_texture_table` API. Font textures are auto-registered during font loading.
