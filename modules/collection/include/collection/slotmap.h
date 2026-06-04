#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.fwd.h>

#define MEL_SLOTMAP_SENTINEL UINT32_MAX

typedef struct
{
    u32  generation;
    u32  packed_idx;
    u32  next_free;
    bool alive;
    bool held; // removed-deferred: dead but withheld from the free list until reclaimed
} Mel_SlotMap_Slot;

struct Mel_SlotMap
{
    Mel_SlotMap_Slot* slots;
    u32*              packed_to_slot;
    u8*               data;
    u32               slot_count;
    u32               slot_capacity;
    u32               packed_count;
    u32               packed_capacity;
    usize             item_size;
    u32               free_head;
    const Mel_Alloc*  alloc;
};

typedef struct
{
    usize item_size;
    u32   initial_capacity;
} Mel_SlotMap_Opt;

void mel_slotmap_init_opt(Mel_SlotMap* sm, const Mel_Alloc* alloc, Mel_SlotMap_Opt opt);
#define mel_slotmap_init(sm, alloc, ...) mel_slotmap_init_opt((sm), (alloc), (Mel_SlotMap_Opt){ __VA_ARGS__ })

void               mel_slotmap_free(Mel_SlotMap* sm);
Mel_SlotMap_Handle mel_slotmap_insert(Mel_SlotMap* sm, const void* item);
void*              mel_slotmap_get(Mel_SlotMap* sm, Mel_SlotMap_Handle handle);
bool               mel_slotmap_remove(Mel_SlotMap* sm, Mel_SlotMap_Handle handle);
bool               mel_slotmap_alive(Mel_SlotMap* sm, Mel_SlotMap_Handle handle);
u32                mel_slotmap_count(Mel_SlotMap* sm);
void*              mel_slotmap_data(Mel_SlotMap* sm);

// Two-phase removal for future-gated reclamation (gpu-rhi.md §3.3 retirement). remove_deferred marks the
// slot dead and rolls its generation immediately (use-after-free stays a loud failure) and swap-removes the
// dense payload, but withholds the index from the free list; reclaim returns that index for reuse once the
// caller's retirement condition is met. The handle resolves to NULL between the two calls.
bool mel_slotmap_remove_deferred(Mel_SlotMap* sm, Mel_SlotMap_Handle handle);
bool mel_slotmap_reclaim(Mel_SlotMap* sm, u32 index);
