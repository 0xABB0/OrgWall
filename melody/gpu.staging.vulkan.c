#include "gpu.staging.h"
#include "gpu.device.vulkan.h"
#include "gpu.cmd.h"
#include "gpu.types.vulkan.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

void mel_staging_init_opt(Mel_Staging* stg, Mel_Staging_Opt opt)
{
    assert(stg != nullptr);
    assert(opt.dev != nullptr);
    assert(opt.buffer_size > 0);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    *stg = (Mel_Staging){0};
    stg->alloc = alloc;

    mel_gpu_buffer_init(&stg->buffer, opt.dev,
        .size = opt.buffer_size,
        .usage = MEL_GPU_BUFFER_USAGE_TRANSFER_SRC,
        .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU,
    );

    mel_arena_init(&stg->arena, stg->buffer.mapped, (usize)opt.buffer_size);

    u32 initial_copies = 64;
    stg->copies = mel_alloc_array(alloc, Mel_Staging_Copy, initial_copies);
    stg->copy_capacity = initial_copies;
    stg->copy_count = 0;
}

void mel_staging_shutdown(Mel_Staging* stg, Mel_Gpu_Device* dev)
{
    assert(stg != nullptr);
    assert(dev != nullptr);

    mel_gpu_buffer_shutdown(&stg->buffer, dev);
    if (stg->copies)
        mel_dealloc(stg->alloc, stg->copies);
    *stg = (Mel_Staging){0};
}

void mel_staging_write(Mel_Staging* stg, Mel_Gpu_Buffer* dst, u64 offset, const void* data, u64 size)
{
    assert(stg != nullptr);
    assert(dst != nullptr);
    assert(data != nullptr);
    assert(size > 0);

    void* staging_ptr = mel_arena_push(&stg->arena, (usize)size);
    memcpy(staging_ptr, data, (usize)size);

    u64 src_offset = (u64)((u8*)staging_ptr - stg->arena.base);

    if (stg->copy_count >= stg->copy_capacity)
    {
        u32 new_cap = stg->copy_capacity * 2;
        stg->copies = mel_realloc(stg->alloc, stg->copies, sizeof(Mel_Staging_Copy) * new_cap);
        stg->copy_capacity = new_cap;
    }

    stg->copies[stg->copy_count++] = (Mel_Staging_Copy){
        .dst = dst,
        .dst_offset = offset,
        .src_offset = src_offset,
        .size = size,
    };
}

void mel_staging_flush(Mel_Staging* stg, Mel_Gpu_Cmd* c)
{
    assert(stg != nullptr);
    assert(c != nullptr);

    if (stg->copy_count == 0)
        return;

    mel_gpu_buffer_flush(&stg->buffer, c->dev);

    for (u32 i = 0; i < stg->copy_count; i++)
    {
        Mel_Staging_Copy* cp = &stg->copies[i];
        VkBufferCopy region = {
            .srcOffset = cp->src_offset,
            .dstOffset = cp->dst_offset,
            .size = cp->size,
        };
        vkCmdCopyBuffer((VkCommandBuffer)c->_cmd,
            (VkBuffer)stg->buffer._handle, (VkBuffer)cp->dst->_handle, 1, &region);
    }
}

void mel_staging_reset(Mel_Staging* stg)
{
    assert(stg != nullptr);
    mel_arena_reset(&stg->arena);
    stg->copy_count = 0;
}
