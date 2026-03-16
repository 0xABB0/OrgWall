#pragma once

#ifdef _CLANGD
#include "collection.pool.h"
#endif

static inline u32 mel__pool_tag_index(u64 tag) { return (u32)(tag & 0xFFFFFFFF); }
static inline u32 mel__pool_tag_gen(u64 tag) { return (u32)(tag >> 32); }
static inline u64 mel__pool_tag_pack(u32 index, u32 gen) { return (u64)index | ((u64)gen << 32); }

inline static bool mel_pool_owns(Mel_Pool* pool, void* ptr)
{
    assert(pool != NULL);
    u8* p = (u8*)ptr;
    return p >= pool->base && p < pool->base + pool->block_size * pool->block_count;
}

inline static void* mel_pool_alloc(Mel_Pool* pool)
{
    assert(pool != NULL);

    u64 old = atomic_load_explicit(&pool->free_stack, memory_order_acquire);
    for (;;) {
        u32 idx = mel__pool_tag_index(old);
        u32 gen = mel__pool_tag_gen(old);
        assert(idx != MEL_POOL_NULL_INDEX);

        u32 next = *(u32*)(pool->base + (usize)idx * pool->block_size);
        u64 desired = mel__pool_tag_pack(next, gen + 1);

        if (atomic_compare_exchange_weak_explicit(&pool->free_stack, &old, desired,
                memory_order_release, memory_order_acquire))
        {
            atomic_fetch_add_explicit(&pool->used_count, 1, memory_order_relaxed);
#if MEL_COLLECTION_POOL_DEBUG
            atomic_fetch_add_explicit(&pool->alloc_count, 1, memory_order_relaxed);
            usize used = atomic_load_explicit(&pool->used_count, memory_order_relaxed);
            usize peak = atomic_load_explicit(&pool->peak_used, memory_order_relaxed);
            while (used > peak) {
                if (atomic_compare_exchange_weak_explicit(&pool->peak_used, &peak, used,
                        memory_order_relaxed, memory_order_relaxed))
                    break;
            }
#endif
            return pool->base + (usize)idx * pool->block_size;
        }
    }
}

inline static void mel_pool_free(Mel_Pool* pool, void* ptr)
{
    assert(pool != NULL);
    assert(ptr != NULL);
    assert(mel_pool_owns(pool, ptr));

    u32 freed_idx = (u32)(((u8*)ptr - pool->base) / pool->block_size);
    u64 old = atomic_load_explicit(&pool->free_stack, memory_order_relaxed);

    for (;;) {
        u32 head_idx = mel__pool_tag_index(old);
        u32 gen = mel__pool_tag_gen(old);
        *(u32*)ptr = head_idx;
        u64 desired = mel__pool_tag_pack(freed_idx, gen + 1);

        if (atomic_compare_exchange_weak_explicit(&pool->free_stack, &old, desired,
                memory_order_release, memory_order_relaxed))
        {
            atomic_fetch_sub_explicit(&pool->used_count, 1, memory_order_relaxed);
#if MEL_COLLECTION_POOL_DEBUG
            atomic_fetch_add_explicit(&pool->free_count, 1, memory_order_relaxed);
#endif
            return;
        }
    }
}
