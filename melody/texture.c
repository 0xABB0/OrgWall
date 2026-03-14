#include "texture.h"
#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "string.str8.h"
// ASYNC_V2: VFS removed
// #include "vfs.h"
#include "allocator.h"
#include <SDL3/SDL.h>
#include <tracy/TracyC.h>

bool mel_texture_load(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Vfs* vfs, const Mel_Alloc* alloc, str8 path)
{
    // ASYNC_V2: VFS removed
    (void)tex; (void)dev; (void)vfs; (void)alloc;
    SDL_Log("Failed to load texture (VFS removed): %.*s", (int)path.len, path.data);
    return false;
}

bool mel_texture_load_and_bind(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, Mel_Vfs* vfs, const Mel_Alloc* alloc, str8 path)
{
    // ASYNC_V2: VFS removed
    (void)pipeline;
    if (!mel_texture_load(tex, dev, vfs, alloc, path))
        return false;
    return true;
}
