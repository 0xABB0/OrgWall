#include "texture.h"
#include "string.str8.h"
#include "assets.h"
#include <SDL3/SDL.h>

bool mel_texture_load(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, str8 path)
{
    assert(tex != nullptr);
    assert(dev != nullptr);
    assert(!str8_is_empty(path));

    u32 size;
    u8* data = mel_assets_read(path, &size);
    if (!data)
    {
        SDL_Log("Failed to load texture: %.*s", (int)path.len, path.data);
        return false;
    }

    mel_gpu_texture_init(tex, dev, .data = data, .data_size = size);

    mel_assets_free(data);

    SDL_Log("Loaded texture: %.*s", (int)path.len, path.data);

    return true;
}

bool mel_texture_load_and_bind(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, str8 path)
{
    assert(pipeline != nullptr);

    if (!mel_texture_load(tex, dev, path))
    {
        return false;
    }

    tex->descriptor = mel_gpu_pipeline_alloc_descriptor(pipeline, dev);
    mel_gpu_pipeline_write_texture(pipeline, dev, tex->descriptor, tex->image.view, tex->sampler);

    return true;
}
