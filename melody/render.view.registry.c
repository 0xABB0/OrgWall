#include "render.view.registry.h"
#include "render.viewport.h"
#include "collection.slotmap.h"
#include "allocator.heap.h"

#include <assert.h>

static Mel_SlotMap s_views;
static bool s_initialized;

__attribute__((constructor))
static void mel__view_registry_init(void)
{
    mel_slotmap_init(&s_views, mel_alloc_heap(),
        .item_size = sizeof(Mel_Render_View), .initial_capacity = 8);
    s_initialized = true;
}

__attribute__((destructor))
static void mel__view_registry_shutdown(void)
{
    if (!s_initialized) return;
    mel_slotmap_free(&s_views);
    s_initialized = false;
}

Mel_SlotMap_Handle mel__view_registry_insert(const Mel_Render_View* view)
{
    assert(s_initialized);
    assert(view != nullptr);
    return mel_slotmap_insert(&s_views, view);
}

void mel__view_registry_remove(Mel_SlotMap_Handle handle)
{
    assert(s_initialized);
    mel_slotmap_remove(&s_views, handle);
}

Mel_Render_View* mel__view_registry_get(Mel_SlotMap_Handle handle)
{
    assert(s_initialized);
    return mel_slotmap_get(&s_views, handle);
}

bool mel__view_registry_alive(Mel_SlotMap_Handle handle)
{
    if (!s_initialized) return false;
    return mel_slotmap_alive(&s_views, handle);
}

u32 mel__view_registry_count(void)
{
    return s_initialized ? mel_slotmap_count(&s_views) : 0;
}

Mel_Render_View* mel__view_registry_data(void)
{
    assert(s_initialized);
    return mel_slotmap_data(&s_views);
}

Mel_Render_View* mel__view_registry_at(u32 packed_index)
{
    assert(s_initialized);
    assert(packed_index < mel_slotmap_count(&s_views));
    Mel_Render_View* views = mel_slotmap_data(&s_views);
    return &views[packed_index];
}

Mel_Render_View_Handle mel__view_registry_handle_at(u32 packed_index)
{
    assert(s_initialized);
    assert(packed_index < mel_slotmap_count(&s_views));

    u32 slot_idx = s_views.packed_to_slot[packed_index];
    Mel_SlotMap_Slot* slot = &s_views.slots[slot_idx];
    return (Mel_Render_View_Handle){
        .handle = mel_slotmap_handle_make(slot_idx, slot->generation),
    };
}
