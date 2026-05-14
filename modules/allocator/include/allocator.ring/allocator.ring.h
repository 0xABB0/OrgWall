#pragma once
#include <core/types.h>
#include "cfg.h"

typedef struct Mel_Ring_Header {
    usize size;
} Mel_Ring_Header;

typedef struct Mel_Ring_Alloc {
    u8*   base;
    usize size;
    usize write_offset;
    usize read_offset;
    usize used;
#if MEL_ALLOCATOR_RING_DEBUG
    usize peak_used;
    usize push_count;
    usize pop_count;
    const char* name;
#endif
} Mel_Ring_Alloc;

void   mel_ring_init(Mel_Ring_Alloc* ring, void* buffer, usize size);
void*  mel_ring_push(Mel_Ring_Alloc* ring, usize size);
void   mel_ring_pop(Mel_Ring_Alloc* ring);
void*  mel_ring_peek(Mel_Ring_Alloc* ring);
void   mel_ring_reset(Mel_Ring_Alloc* ring);
usize  mel_ring_available(Mel_Ring_Alloc* ring);

#define mel_ring_push_struct(r, T) (T*)mel_ring_push((r), sizeof(T))
#define mel_ring_push_array(r, T, count) (T*)mel_ring_push((r), sizeof(T) * (count))

#include "allocator.ring.inl"
