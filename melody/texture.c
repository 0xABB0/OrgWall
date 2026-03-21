#include "texture.h"
#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "string.str8.h"
#include "vfs.h"
#include "allocator.h"
#include "log.h"
#include <tracy/TracyC.h>

bool mel_texture_load(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, const Mel_Alloc* alloc, str8 path)
{
    i64 fsize = 0;
    u8* file_data = mel_vfs_read_file(path, &fsize, alloc);
    if (!file_data)
    {
        mel_log_error("texture", "Failed to read texture: %.*s", (int)path.len, path.data);
        return false;
    }

    mel_gpu_texture_init(tex, dev, .data = file_data, .data_size = (u32)fsize, .alloc = alloc);
    mel_dealloc(alloc, file_data);
    return true;
}

bool mel_texture_load_and_bind(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, const Mel_Alloc* alloc, str8 path)
{
    (void)pipeline;
    return mel_texture_load(tex, dev, alloc, path);
}
