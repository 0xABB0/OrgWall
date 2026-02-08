#include "texture.h"
#include "assets.h"
#include <SDL3/SDL.h>

bool mel_texture_load(Mel_VkTexture* tex, Mel_VkContext* ctx, const char* path)
{
    assert(tex != nullptr);
    assert(ctx != nullptr);
    assert(path != nullptr);

    u32 size;
    u8* data = mel_assets_read(path, &size);
    if (!data)
    {
        SDL_Log("Failed to load texture: %s", path);
        return false;
    }

    bool result = mel_vk_texture_init(tex, ctx, .data = data, .data_size = size);

    mel_assets_free(data);

    if (result)
    {
        SDL_Log("Loaded texture: %s", path);
    }

    return result;
}

bool mel_texture_load_and_bind(Mel_VkTexture* tex, Mel_VkContext* ctx, Mel_VkPipeline* pipeline, const char* path)
{
    assert(pipeline != nullptr);

    if (!mel_texture_load(tex, ctx, path))
    {
        return false;
    }

    tex->descriptor = mel_vk_pipeline_alloc_descriptor(pipeline, ctx);
    mel_vk_pipeline_write_texture(pipeline, ctx, tex->descriptor, tex->image.view, tex->sampler);

    return true;
}
