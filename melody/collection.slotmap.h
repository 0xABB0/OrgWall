#pragma once

#include "types.h"
#include "allocator.fwd.h"
#include "collection.slotmap.fwd.h"

#define MEL_SLOTMAP_GEN_BITS  14
#define MEL_SLOTMAP_IDX_BITS  18
#define MEL_SLOTMAP_MAX_INDEX ((1u << MEL_SLOTMAP_IDX_BITS) - 1)
#define MEL_SLOTMAP_MAX_GEN   ((1u << MEL_SLOTMAP_GEN_BITS) - 1)

typedef struct {
    u16 generation;
    u32 packed_idx;
    u32 next_free;
    bool alive;
} Mel_SlotMap_Slot;

struct Mel_SlotMap {
    Mel_SlotMap_Slot* slots;
    u32* packed_to_slot;
    u8* data;
    u32 slot_count;
    u32 slot_capacity;
    u32 packed_count;
    u32 packed_capacity;
    usize item_size;
    u32 free_head;
    const Mel_Alloc* alloc;
};

typedef struct {
    usize item_size;
    u32 initial_capacity;
} Mel_SlotMap_Opt;

static inline Mel_SlotMap_Handle mel_slotmap_handle_pack(u32 idx, u16 gen)
{
    return (Mel_SlotMap_Handle){ .value = ((u32)gen << MEL_SLOTMAP_IDX_BITS) | idx };
}

static inline u32 mel_slotmap_handle_index(Mel_SlotMap_Handle h)
{
    return h.value & MEL_SLOTMAP_MAX_INDEX;
}

static inline u16 mel_slotmap_handle_gen(Mel_SlotMap_Handle h)
{
    return (u16)(h.value >> MEL_SLOTMAP_IDX_BITS);
}

static inline bool mel_slotmap_handle_valid(Mel_SlotMap_Handle h)
{
    return h.value != 0;
}

void              mel_slotmap_init_opt(Mel_SlotMap* sm, const Mel_Alloc* alloc, Mel_SlotMap_Opt opt);
#define mel_slotmap_init(sm, alloc, ...) mel_slotmap_init_opt((sm), (alloc), (Mel_SlotMap_Opt){__VA_ARGS__})

void              mel_slotmap_free(Mel_SlotMap* sm);
Mel_SlotMap_Handle mel_slotmap_insert(Mel_SlotMap* sm, const void* item);
void*             mel_slotmap_get(Mel_SlotMap* sm, Mel_SlotMap_Handle handle);
bool              mel_slotmap_remove(Mel_SlotMap* sm, Mel_SlotMap_Handle handle);
bool              mel_slotmap_alive(Mel_SlotMap* sm, Mel_SlotMap_Handle handle);
u32               mel_slotmap_count(Mel_SlotMap* sm);
void*             mel_slotmap_data(Mel_SlotMap* sm);
