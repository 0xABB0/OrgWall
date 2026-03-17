#include "gpu.indirect.h"
#include "gpu.device.h"
#include "gpu.cmd.h"
#include "gpu.types.vulkan.h"
#include "allocator.heap.h"

#include <string.h>

void mel_indirect_init_opt(Mel_Indirect_Draw* ind, Mel_Indirect_Draw_Opt opt)
{
    assert(ind != nullptr);
    assert(opt.dev != nullptr);
    assert(opt.stride > 0);

    *ind = (Mel_Indirect_Draw){0};
    ind->stride = opt.stride;
    ind->gpu_writable = opt.gpu_writable;

    mel_array_init(&ind->commands, mel_alloc_heap());

    u32 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 64;
    mel_array_reserve(&ind->commands, cap * opt.stride);

    Mel_Gpu_Buffer_Usage usage = MEL_GPU_BUFFER_USAGE_INDIRECT | MEL_GPU_BUFFER_USAGE_TRANSFER_DST;
    if (opt.gpu_writable)
        usage |= MEL_GPU_BUFFER_USAGE_STORAGE;

    mel_gpu_buffer_init(&ind->buffer, opt.dev,
        .size = cap * opt.stride,
        .usage = usage,
        .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU,
    );
}

void mel_indirect_shutdown(Mel_Indirect_Draw* ind, Mel_Gpu_Device* dev)
{
    assert(ind != nullptr);
    assert(dev != nullptr);

    mel_gpu_buffer_shutdown(&ind->buffer, dev);
    mel_array_free(&ind->commands);
    *ind = (Mel_Indirect_Draw){0};
}

void mel_indirect_append(Mel_Indirect_Draw* ind, const void* cmd)
{
    assert(ind != nullptr);
    assert(cmd != nullptr);

    const u8* src = (const u8*)cmd;
    for (usize i = 0; i < ind->stride; i++)
        mel_array_push(&ind->commands, src[i]);

    ind->count++;
}

void mel_indirect_clear(Mel_Indirect_Draw* ind)
{
    assert(ind != nullptr);
    mel_array_clear(&ind->commands);
    ind->count = 0;
}

u32 mel_indirect_count(Mel_Indirect_Draw* ind)
{
    assert(ind != nullptr);
    return ind->count;
}

void mel_indirect_upload(Mel_Indirect_Draw* ind, Mel_Gpu_Device* dev)
{
    assert(ind != nullptr);
    assert(dev != nullptr);

    if (ind->count == 0)
        return;

    u64 needed = (u64)ind->count * ind->stride;
    if (needed > ind->buffer.size)
    {
        mel_gpu_buffer_shutdown(&ind->buffer, dev);

        Mel_Gpu_Buffer_Usage usage = MEL_GPU_BUFFER_USAGE_INDIRECT | MEL_GPU_BUFFER_USAGE_TRANSFER_DST;
        if (ind->gpu_writable)
            usage |= MEL_GPU_BUFFER_USAGE_STORAGE;

        mel_gpu_buffer_init(&ind->buffer, dev,
            .size = needed,
            .usage = usage,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU,
        );
    }

    mel_gpu_buffer_upload(&ind->buffer, dev, ind->commands.items, needed, 0);
}

void mel_indirect_bind(Mel_Indirect_Draw* ind, Mel_Gpu_Cmd* c)
{
    assert(ind != nullptr);
    assert(c != nullptr);
    assert(ind->buffer._handle != nullptr);

    mel_gpu_cmd_draw_indexed_indirect(c, &ind->buffer, 0, ind->count, (u32)ind->stride);
}
