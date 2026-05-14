#include <collection.slotmap/slotmap.h>
#include <allocator/allocator.h>

#include <string.h>

static void mel__slotmap_grow_slots(Mel_SlotMap* sm, u32 new_cap)
{
    assert(new_cap > sm->slot_capacity);

    sm->slots = mel_realloc(sm->alloc, sm->slots, sizeof(Mel_SlotMap_Slot) * new_cap);

    for (u32 i = sm->slot_capacity; i < new_cap; i++)
    {
        sm->slots[i] = (Mel_SlotMap_Slot){
            .generation = 1,
            .packed_idx = 0,
            .next_free = i + 1,
            .alive = false,
        };
    }
    sm->slots[new_cap - 1].next_free = MEL_SLOTMAP_SENTINEL;

    if (sm->free_head == MEL_SLOTMAP_SENTINEL)
        sm->free_head = sm->slot_capacity;
    else
    {
        u32 tail = sm->free_head;
        while (sm->slots[tail].next_free != MEL_SLOTMAP_SENTINEL)
            tail = sm->slots[tail].next_free;
        sm->slots[tail].next_free = sm->slot_capacity;
    }

    sm->slot_capacity = new_cap;
}

static void mel__slotmap_grow_packed(Mel_SlotMap* sm, u32 new_cap)
{
    assert(new_cap > sm->packed_capacity);

    sm->packed_to_slot = mel_realloc(sm->alloc, sm->packed_to_slot, sizeof(u32) * new_cap);
    sm->data = mel_realloc(sm->alloc, sm->data, sm->item_size * new_cap);

    sm->packed_capacity = new_cap;
}

void mel_slotmap_init_opt(Mel_SlotMap* sm, const Mel_Alloc* alloc, Mel_SlotMap_Opt opt)
{
    assert(sm != nullptr);
    assert(alloc != nullptr);
    assert(opt.item_size > 0);

    *sm = (Mel_SlotMap){0};
    sm->alloc = alloc;
    sm->item_size = opt.item_size;
    sm->free_head = MEL_SLOTMAP_SENTINEL;

    u32 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 16;

    sm->slots = mel_calloc(alloc, sizeof(Mel_SlotMap_Slot) * cap);
    for (u32 i = 0; i < cap; i++)
    {
        sm->slots[i] = (Mel_SlotMap_Slot){
            .generation = 1,
            .packed_idx = 0,
            .next_free = i + 1,
            .alive = false,
        };
    }
    sm->slots[cap - 1].next_free = MEL_SLOTMAP_SENTINEL;
    sm->free_head = 0;
    sm->slot_capacity = cap;

    sm->packed_to_slot = mel_calloc(alloc, sizeof(u32) * cap);
    sm->data = mel_calloc(alloc, sm->item_size * cap);
    sm->packed_capacity = cap;
    sm->packed_count = 0;
}

void mel_slotmap_free(Mel_SlotMap* sm)
{
    assert(sm != nullptr);

    if (sm->slots)
        mel_dealloc(sm->alloc, sm->slots);
    if (sm->packed_to_slot)
        mel_dealloc(sm->alloc, sm->packed_to_slot);
    if (sm->data)
        mel_dealloc(sm->alloc, sm->data);

    *sm = (Mel_SlotMap){0};
}

Mel_SlotMap_Handle mel_slotmap_insert(Mel_SlotMap* sm, const void* item)
{
    assert(sm != nullptr);
    assert(item != nullptr);

    if (sm->free_head == MEL_SLOTMAP_SENTINEL)
    {
        u32 new_cap = sm->slot_capacity * 2;
        mel__slotmap_grow_slots(sm, new_cap);
    }

    if (sm->packed_count >= sm->packed_capacity)
    {
        mel__slotmap_grow_packed(sm, sm->packed_capacity * 2);
    }

    u32 slot_idx = sm->free_head;
    Mel_SlotMap_Slot* slot = &sm->slots[slot_idx];
    sm->free_head = slot->next_free;

    u32 packed_idx = sm->packed_count;
    slot->packed_idx = packed_idx;
    slot->alive = true;

    sm->packed_to_slot[packed_idx] = slot_idx;
    memcpy(sm->data + packed_idx * sm->item_size, item, sm->item_size);
    sm->packed_count++;
    sm->slot_count++;

    return mel_slotmap_handle_make(slot_idx, slot->generation);
}

void* mel_slotmap_get(Mel_SlotMap* sm, Mel_SlotMap_Handle handle)
{
    assert(sm != nullptr);

    if (!mel_slotmap_handle_valid(handle))
        return nullptr;

    u32 idx = handle.index;
    u32 gen = handle.generation;

    if (idx >= sm->slot_capacity)
        return nullptr;

    Mel_SlotMap_Slot* slot = &sm->slots[idx];
    if (!slot->alive || slot->generation != gen)
        return nullptr;

    return sm->data + slot->packed_idx * sm->item_size;
}

bool mel_slotmap_remove(Mel_SlotMap* sm, Mel_SlotMap_Handle handle)
{
    assert(sm != nullptr);

    if (!mel_slotmap_handle_valid(handle))
        return false;

    u32 idx = handle.index;
    u32 gen = handle.generation;

    if (idx >= sm->slot_capacity)
        return false;

    Mel_SlotMap_Slot* slot = &sm->slots[idx];
    if (!slot->alive || slot->generation != gen)
        return false;

    u32 packed_idx = slot->packed_idx;
    u32 last_packed = sm->packed_count - 1;

    if (packed_idx != last_packed)
    {
        memcpy(sm->data + packed_idx * sm->item_size,
               sm->data + last_packed * sm->item_size,
               sm->item_size);

        u32 moved_slot_idx = sm->packed_to_slot[last_packed];
        sm->packed_to_slot[packed_idx] = moved_slot_idx;
        sm->slots[moved_slot_idx].packed_idx = packed_idx;
    }

    sm->packed_count--;
    sm->slot_count--;

    slot->alive = false;
    slot->generation++;
    slot->next_free = sm->free_head;
    sm->free_head = idx;

    return true;
}

bool mel_slotmap_alive(Mel_SlotMap* sm, Mel_SlotMap_Handle handle)
{
    assert(sm != nullptr);

    if (!mel_slotmap_handle_valid(handle))
        return false;

    u32 idx = handle.index;
    u32 gen = handle.generation;

    if (idx >= sm->slot_capacity)
        return false;

    Mel_SlotMap_Slot* slot = &sm->slots[idx];
    return slot->alive && slot->generation == gen;
}

u32 mel_slotmap_count(Mel_SlotMap* sm)
{
    assert(sm != nullptr);
    return sm->packed_count;
}

void* mel_slotmap_data(Mel_SlotMap* sm)
{
    assert(sm != nullptr);
    return sm->data;
}
