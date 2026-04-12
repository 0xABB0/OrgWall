#include "texture.h"
#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "string.str8.h"
#include "vfs.h"
#include "allocator.h"
#include "log.h"
#include <tracy/TracyC.h>

bool mel_texture_load_opt(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, const Mel_Alloc* alloc, str8 path,
                          Mel_Texture_Load_Opt opt)
{
    i64 fsize = 0;
    u8* file_data = mel_vfs_read_file(path, &fsize, alloc);
    if (!file_data)
    {
        mel_log_error("texture", "Failed to read texture: %.*s", (int)path.len, path.data);
        return false;
    }

    mel_gpu_texture_init(tex, dev,
        .data = file_data,
        .data_size = (u32)fsize,
        .alloc = alloc,
        .format = opt.format ? opt.format : MEL_GPU_FORMAT_R8G8B8A8_SRGB,
        .nearest_filter = opt.nearest_filter,
        .generate_mips = opt.generate_mips,
        .address_mode_u = opt.address_mode_u,
        .address_mode_v = opt.address_mode_v,
        .address_mode_w = opt.address_mode_w);
    mel_dealloc(alloc, file_data);
    return true;
}

bool mel_texture_load_and_bind_opt(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline* pipeline, const Mel_Alloc* alloc, str8 path,
                                   Mel_Texture_Load_Opt opt)
{
    (void)pipeline;
    return mel_texture_load_opt(tex, dev, alloc, path, opt);
}
