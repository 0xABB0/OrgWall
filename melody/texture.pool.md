# texture.pool

Shared texture object pool plus bindless-table integration.

## Contract

The pool deduplicates loaded textures by:

- asset path
- requested GPU format
- requested filtering intent

That is required for correctness. The same image path may need different interpretations depending on whether it is sampled as color data or as linear material data.

`mel_texture_pool_load(...)` keeps the default behavior if no options are provided, but callers can request explicit non-default formats when needed.

This matters directly for scenes like Sponza:

- base-color textures want `sRGB`
- normal maps want linear `UNORM`
- metallic-roughness textures want linear `UNORM`
