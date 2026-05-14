#include <allocator/allocator.h>
#include <collection.pool/collection.pool.h>

void mel_pool_init_opt(Mel_Pool* pool, void* buffer, usize buffer_size, Mel_Pool_Init_Opt opt)
{
    assert(pool != NULL);
    assert(buffer != NULL);
    assert(buffer_size > 0);
    assert(opt.block_size >= sizeof(void*));

    pool->base        = (u8*)buffer;
    pool->block_size  = opt.block_size;
    pool->block_count = buffer_size / opt.block_size;
    atomic_store_explicit(&pool->used_count, 0, memory_order_relaxed);

#if MEL_COLLECTION_POOL_DEBUG
    atomic_store_explicit(&pool->peak_used, 0, memory_order_relaxed);
    atomic_store_explicit(&pool->alloc_count, 0, memory_order_relaxed);
    atomic_store_explicit(&pool->free_count, 0, memory_order_relaxed);
    pool->name = NULL;
#endif

    mel_pool_reset(pool);
}

void mel_pool_reset(Mel_Pool* pool)
{
    assert(pool != NULL);

    atomic_store_explicit(&pool->used_count, 0, memory_order_relaxed);

    for (usize i = 0; i < pool->block_count; i++)
    {
        u8* block = pool->base + i * pool->block_size;
        u32 next = (i + 1 < pool->block_count) ? (u32)(i + 1) : MEL_POOL_NULL_INDEX;
        *(u32*)block = next;
    }

    u64 initial = pool->block_count > 0
        ? mel__pool_tag_pack(0, 0)
        : mel__pool_tag_pack(MEL_POOL_NULL_INDEX, 0);
    atomic_store_explicit(&pool->free_stack, initial, memory_order_release);
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
