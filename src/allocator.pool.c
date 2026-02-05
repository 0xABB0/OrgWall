#include "allocator.h"
#include "allocator.pool.h"

void mel_pool_init_opt(Mel_Pool* pool, void* buffer, usize buffer_size, Mel_Pool_Init_Opt opt)
{
    assert(pool != NULL);
    assert(buffer != NULL);
    assert(buffer_size > 0);
    assert(opt.block_size >= sizeof(void*));

    pool->base        = (u8*)buffer;
    pool->block_size  = opt.block_size;
    pool->block_count = buffer_size / opt.block_size;
    pool->used_count  = 0;
    pool->free_list   = NULL;

#if MEL_ALLOCATOR_POOL_DEBUG
    pool->peak_used   = 0;
    pool->alloc_count = 0;
    pool->free_count  = 0;
    pool->name        = NULL;
#endif

    mel_pool_reset(pool);
}

void mel_pool_reset(Mel_Pool* pool)
{
    assert(pool != NULL);

    pool->used_count = 0;
    pool->free_list  = NULL;

    for (usize i = 0; i < pool->block_count; i++)
    {
        u8* block = pool->base + i * pool->block_size;
        *(void**)block = pool->free_list;
        pool->free_list = block;
    }
}

static void* mel__pool_alloc_cb(void* ptr, usize size, u32 align,
                                const char* file, const char* func, u32 line,
                                void* user_data)
{
    MEL_UNUSED(align);
    MEL_UNUSED(file);
    MEL_UNUSED(func);
    MEL_UNUSED(line);

    Mel_Pool* pool = (Mel_Pool*)user_data;

    if (ptr == NULL && size > 0)
    {
        return mel_pool_alloc(pool);
    }

    if (ptr != NULL && size == 0)
    {
        mel_pool_free(pool, ptr);
        return NULL;
    }

    assert(false);
    return NULL;
}

Mel_Alloc mel_pool_to_alloc(Mel_Pool* pool)
{
    return (Mel_Alloc){
        .alloc_cb  = mel__pool_alloc_cb,
        .user_data = pool,
    };
}
