#pragma once

#include "types.h"

#include "allocator.pool.config.h"
#include "allocator.fwd.h"

typedef struct Mel_Pool {
    u8*   base;
    usize block_size;
    usize block_count;
    usize used_count;
    void* free_list;
#if MEL_ALLOCATOR_POOL_DEBUG
    usize       peak_used;
    usize       alloc_count;
    usize       free_count;
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

#include "allocator.pool.inl"
