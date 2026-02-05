#pragma once

#ifdef _CLANGD
#include "allocator.pool.h"
#endif

inline static bool mel_pool_owns(Mel_Pool* pool, void* ptr)
{
    assert(pool != NULL);
    u8* p = (u8*)ptr;
    return p >= pool->base && p < pool->base + pool->block_size * pool->block_count;
}

inline static void* mel_pool_alloc(Mel_Pool* pool)
{
    assert(pool != NULL);
    assert(pool->free_list != NULL);

    void* block = pool->free_list;
    pool->free_list = *(void**)block;
    pool->used_count++;

#if MEL_ALLOCATOR_POOL_DEBUG
    pool->alloc_count++;
    if (pool->used_count > pool->peak_used)
    {
        pool->peak_used = pool->used_count;
    }
#endif

    return block;
}

inline static void mel_pool_free(Mel_Pool* pool, void* ptr)
{
    assert(pool != NULL);
    assert(ptr != NULL);
    assert(mel_pool_owns(pool, ptr));

    *(void**)ptr = pool->free_list;
    pool->free_list = ptr;
    pool->used_count--;

#if MEL_ALLOCATOR_POOL_DEBUG
    pool->free_count++;
#endif
}
