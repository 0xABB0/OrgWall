#pragma once
#include "core.types.h"
#include "allocator.block.cfg.h"

typedef struct Mel_Block_Header {
    usize size;
    usize data_offset;
} Mel_Block_Header;

typedef struct Mel_Block_Alloc {
    u8*   base;
    usize size;
    usize offset;
#if MEL_ALLOCATOR_BLOCK_DEBUG
    usize peak_used;
    usize push_count;
    usize reset_count;
    const char* name;
#endif
} Mel_Block_Alloc;

typedef struct Mel_Block_Iter {
    Mel_Block_Alloc* alloc;
    usize offset;
} Mel_Block_Iter;

void  mel_block_init(Mel_Block_Alloc* alloc, void* buffer, usize size);
void* mel_block_push(Mel_Block_Alloc* alloc, usize size, usize align);
void  mel_block_reset(Mel_Block_Alloc* alloc);

Mel_Block_Iter mel_block_iter_begin(Mel_Block_Alloc* alloc);
void*          mel_block_iter_next(Mel_Block_Iter* iter, usize* out_size);
bool           mel_block_iter_end(Mel_Block_Iter* iter);

#define mel_block_push_struct(a, T) (T*)mel_block_push((a), sizeof(T), _Alignof(T))
#define mel_block_push_array(a, T, count) (T*)mel_block_push((a), sizeof(T) * (count), _Alignof(T))

#include "allocator.block.inl"
