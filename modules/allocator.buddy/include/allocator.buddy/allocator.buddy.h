#pragma once
#include "core.types.h"
#include "cfg.h"

#define MEL_BUDDY_FREE  0
#define MEL_BUDDY_SPLIT 1
#define MEL_BUDDY_USED  2

typedef struct Mel_Buddy_Alloc {
    u8*   base;
    usize size;
    i32   levels;
    u8*   tree;
    usize tree_size;
    usize min_block;
#if MEL_ALLOCATOR_BUDDY_DEBUG
    usize alloc_count;
    usize free_count;
    usize current_usage;
    usize peak_usage;
    const char* name;
#endif
} Mel_Buddy_Alloc;

typedef struct {
    usize min_block_size;
    u8*   tree_buffer;
} Mel_Buddy_Init_Opt;

void  mel_buddy_init_opt(Mel_Buddy_Alloc* buddy, void* buffer, usize size, Mel_Buddy_Init_Opt);
#define mel_buddy_init(buddy, buffer, size, ...) mel_buddy_init_opt((buddy), (buffer), (size), (Mel_Buddy_Init_Opt){__VA_ARGS__})

void* mel_buddy_alloc(Mel_Buddy_Alloc* buddy, usize size);
void  mel_buddy_free(Mel_Buddy_Alloc* buddy, void* ptr);
void  mel_buddy_reset(Mel_Buddy_Alloc* buddy);

#include "allocator.buddy.inl"
