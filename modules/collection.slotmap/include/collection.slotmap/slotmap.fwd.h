#pragma once

#include "core.types.h"

typedef struct Mel_SlotMap Mel_SlotMap;
typedef struct { u32 index; u32 generation; } Mel_SlotMap_Handle;
#define MEL_SLOTMAP_HANDLE_NULL ((Mel_SlotMap_Handle){0})

static inline Mel_SlotMap_Handle mel_slotmap_handle_make(u32 index, u32 gen)
{
    return (Mel_SlotMap_Handle){ .index = index, .generation = gen };
}

static inline bool mel_slotmap_handle_valid(Mel_SlotMap_Handle h)
{
    return h.index != 0 || h.generation != 0;
}

static inline u64 mel_slotmap_handle_pack64(Mel_SlotMap_Handle h)
{
    return ((u64)h.generation << 32) | (u64)h.index;
}

static inline Mel_SlotMap_Handle mel_slotmap_handle_unpack64(u64 packed)
{
    return (Mel_SlotMap_Handle){ .index = (u32)packed, .generation = (u32)(packed >> 32) };
}

static inline void* mel_slotmap_handle_to_ptr(Mel_SlotMap_Handle h)
{
    return (void*)(usize)mel_slotmap_handle_pack64(h);
}

static inline Mel_SlotMap_Handle mel_slotmap_handle_from_ptr(void* p)
{
    return mel_slotmap_handle_unpack64((u64)(usize)p);
}
