# render.material_base

A material base defines a shader model — the link between object data in the manager and GPU shaders.

## Ownership

Materials own shaders. The pipeline cache owns GPU pipelines. A material base stores a pointer to an externally-owned `Mel_Gpu_Shader` (compiled and managed by the module that registered the material).

## Two-Phase Init

1. **Wire phase**: `mel_material_base_register` reserves a slot with name, param_size, compat. Shader can be NULL.
2. **Compile job**: `mel_material_base_set_shader(id, &shader)` sets the pointer and flips `shader_ready`.

This separation exists because material registration happens during boot wiring (pre-GPU), while shader compilation happens during the gpu_ready cascade.

## Param Size

`param_size = 0` is valid for materials without custom params (e.g., sprites — visual data lives in the manager's info pool). When param_size is 0: no params array allocated, `alloc_instance`/`set_params`/`get_params` assert-crash, `upload_dirty` is a no-op.

For materials with params (text_atlas=16 bytes, text_sdf/msdf=48 bytes): instances are allocated, params stored in a CPU array, uploaded to a GPU storage buffer on `upload_dirty`. The `material_instance` index is passed to the shader via the `layer` field in the render info.

## Compat Flags

- `MEL_COMPAT_FORWARD` (1): forward-rendered 3D
- `MEL_COMPAT_DEFERRED` (2): deferred pipeline
- `MEL_COMPAT_2D` (4): 2D pipeline

Used by the pipeline to validate that a material is appropriate for its rendering context.

## Registered Materials

| Name | Param Size | Owner | Compat |
|------|-----------|-------|--------|
| sprite_2d | 0 | sprite.material.c | 2D |
| text_atlas | 16 | font.atlas.c | 2D |
| text_sdf | 48 | font.sdf.c | 2D |
| text_msdf | 48 | font.msdf.c | 2D |
