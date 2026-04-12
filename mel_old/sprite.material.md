# sprite.material

Registers the "sprite_2d" material base. Owns the sprite_2d.slang shader.

## Boot Flow

1. Constructor: register wire callback
2. Wire: subscribe to `mel_gpu_device_ready` and `mel_shutdown_begin`. Register material base with `param_size=0, compat=MEL_COMPAT_2D, shader=NULL`.
3. gpu_ready: dispatch compile job using phase_counter. Job compiles `shaders/sprite_2d.slang`, then calls `mel_material_base_set_shader`.
4. Shutdown: destroy the shader.

## Notes

`param_size = 0` because sprites carry all per-instance visual data in `Mel_Render_Sprite_Info` (color, UV, texture_idx). No material params buffer.

The ECS 2D source uses `mel_sprite_material_id()` to tag sprite entities with this material's group.
