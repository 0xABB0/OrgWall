# texture

Texture file loading.

This module is the boundary between VFS/file bytes and `gpu.texture`.

## Contract

- `mel_texture_load(...)` reads bytes through the VFS
- decodes them through the GPU texture path
- and forwards explicit format/filter intent

The default path stays color-oriented (`sRGB`) unless the caller explicitly asks otherwise through the load options.

Color textures and data textures are not interchangeable.

- color textures should normally use sRGB formats
- data textures such as normal maps and metallic-roughness maps should use linear/UNORM formats

This module must not silently force every texture through the same color-space interpretation.
