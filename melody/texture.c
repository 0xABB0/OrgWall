#include "texture.h"
#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "string.str8.h"
#include "assets.h"
#include <SDL3/SDL.h>
#include <tracy/TracyC.h>

bool mel_texture_load(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Assets* assets, str8 path)
{
    assert(tex != nullptr);
    assert(dev != nullptr);
    assert(assets != nullptr);
    assert(!str8_is_empty(path));

    TracyCZoneN(ctx, "texture_load", true);

    u32 size;
    u8* data = mel_assets_read(assets, path, &size);
    if (!data)
    {
        SDL_Log("Failed to load texture: %.*s", (int)path.len, path.data);
        TracyCZoneEnd(ctx);
        return false;
    }

    mel_gpu_texture_init(tex, dev, .data = data, .data_size = size);

    mel_assets_free(assets, data);

    SDL_Log("Loaded texture: %.*s", (int)path.len, path.data);

    TracyCZoneEnd(ctx);
    return true;
}

bool mel_texture_load_and_bind(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, Mel_Assets* assets, str8 path)
{
    assert(pipeline != nullptr);
    assert(assets != nullptr);

    if (!mel_texture_load(tex, dev, assets, path))
    {
        return false;
    }

    tex->descriptor = mel_gpu_pipeline_alloc_descriptor(pipeline, dev);
    mel_gpu_pipeline_write_texture(pipeline, dev, tex->descriptor, tex->image.view, tex->sampler);

    return true;
}
