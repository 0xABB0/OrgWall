# render.types.2d

Data layout types for 2D rendering. Defined here (not in the manager) because the manager is layout-agnostic — these types are shared between sources (who write them) and shaders (who read them).

## Mel_Render_Transform_2D (32 bytes)

`{Vec2 pos, Vec2 scale, f32 rotation, f32 depth, u32 flags, u32 _pad}`

Compact 2D transform. The `depth` field controls draw ordering when depth test is enabled (lower depth = closer to camera in world space, but maps to higher z_ndc due to ortho inversion).

## Mel_Render_Sprite_Info (48 bytes)

`{Rect uv, Vec4 color, u32 texture_idx, u32 material_base_id, u32 layer, u32 _pad}`

Per-object render info. `texture_idx` indexes into the bindless texture table. `material_base_id` determines the shader/GPU pipeline. `layer` is repurposed as `material_instance` index for text shaders that need material params.

## Pool Indices

`MEL_2D_POOL_TRANSFORMS = 0`, `MEL_2D_POOL_INFOS = 1`, `MEL_2D_POOL_COUNT = 2`

Convention for 2D manager pool configuration. Not enforced by the manager.
