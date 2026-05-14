#pragma once

#include <core/types.h>

#include "cfg.h"
#include <allocator/fwd.h>
#include <stdatomic.h>

#define MEL_POOL_NULL_INDEX 0xFFFFFFFFu

typedef struct Mel_Pool {
    u8*   base;
    usize block_size;
    usize block_count;
    _Atomic(usize) used_count;
    _Atomic(u64)   free_stack;
#if MEL_COLLECTION_POOL_DEBUG
    _Atomic(usize) peak_used;
    _Atomic(usize) alloc_count;
    _Atomic(usize) free_count;
    const char* name;
#endif
} Mel_Pool;

typedef struct {
    usize block_size;
} Mel_Pool_Init_Opt;

void  mel_pool_init_opt(Mel_Pool* pool, void* buffer, usize buffer_size, Mel_Pool_Init_Opt);
#define mel_pool_init(pool, buffer, buffer_size, ...) mel_pool_init_opt((pool), (buffer), (buffer_size), (Mel_Pool_Init_Opt){__VA_ARGS__})

inline static void* mel_pool_alloc(Mel_Pool* pool);
inline static void  mel_pool_free(Mel_Pool* pool, void* ptr);
inline static bool  mel_pool_owns(Mel_Pool* pool, void* ptr);
void  mel_pool_reset(Mel_Pool* pool);
Mel_Alloc mel_pool_to_alloc(Mel_Pool* pool);

#include "collection.pool.inl"
