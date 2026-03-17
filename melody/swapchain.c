#include "swapchain.h"
#include "window.fwd.h"
#include "gpu.device.h"
#include "collection.slotmap.h"
#include "allocator.heap.h"

#include <SDL3/SDL.h>

static Mel_SlotMap s_swapchains;
static bool s_initialized;

__attribute__((constructor(501)))
static void mel__swapchain_registry_init(void)
{
    mel_slotmap_init(&s_swapchains, mel_alloc_heap(),
        .item_size = sizeof(Mel_Swapchain_Entry), .initial_capacity = 4);
    s_initialized = true;
}

__attribute__((destructor(501)))
static void mel__swapchain_registry_shutdown(void)
{
    if (!s_initialized) return;
    mel_slotmap_free(&s_swapchains);
    s_initialized = false;
}

Mel_Swapchain_Handle mel_swapchain_registry_insert(Mel_Swapchain_Entry* entry)
{
    assert(s_initialized);
    assert(entry != nullptr);

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_swapchains, entry);
    return (Mel_Swapchain_Handle){ .handle = raw };
}

void mel_swapchain_registry_remove(Mel_Swapchain_Handle handle, Mel_Gpu_Device* dev)
{
    assert(s_initialized);

    Mel_Swapchain_Entry* entry = mel_slotmap_get(&s_swapchains, handle.handle);
    assert(entry != nullptr);

    mel_swapchain_shutdown(&entry->swapchain, dev);

    if (entry->_surface != nullptr)
        mel_gpu_surface_destroy(dev, entry->_surface);

    mel_slotmap_remove(&s_swapchains, handle.handle);
}

Mel_Swapchain_Entry* mel_swapchain_registry_get(Mel_Swapchain_Handle handle)
{
    assert(s_initialized);
    Mel_Swapchain_Entry* e = mel_slotmap_get(&s_swapchains, handle.handle);
    assert(e != nullptr);
    return e;
}

Mel_Swapchain_Handle mel_swapchain_registry_find_by_window(Mel_Window_Handle window)
{
    if (!s_initialized) return MEL_SWAPCHAIN_HANDLE_NULL;

    Mel_Swapchain_Entry* entries = mel_slotmap_data(&s_swapchains);
    u32 count = mel_slotmap_count(&s_swapchains);

    for (u32 i = 0; i < count; i++)
    {
        if (entries[i].window.handle.index == window.handle.index &&
            entries[i].window.handle.generation == window.handle.generation)
        {
            u32 slot_idx = s_swapchains.packed_to_slot[i];
            Mel_SlotMap_Slot* slot = &s_swapchains.slots[slot_idx];
            return (Mel_Swapchain_Handle){
                .handle = mel_slotmap_handle_make(slot_idx, slot->generation)
            };
        }
    }

    return MEL_SWAPCHAIN_HANDLE_NULL;
}

u32 mel_swapchain_registry_count(void)
{
    return s_initialized ? mel_slotmap_count(&s_swapchains) : 0;
}

void mel_swapchain_registry_destroy_all(Mel_Gpu_Device* dev)
{
    if (!s_initialized) return;

    Mel_Swapchain_Entry* entries = mel_slotmap_data(&s_swapchains);
    u32 count = mel_slotmap_count(&s_swapchains);

    for (u32 i = 0; i < count; i++)
    {
        mel_swapchain_shutdown(&entries[i].swapchain, dev);
        if (entries[i]._surface != nullptr)
            mel_gpu_surface_destroy(dev, entries[i]._surface);
    }

    mel_slotmap_free(&s_swapchains);
    mel_slotmap_init(&s_swapchains, mel_alloc_heap(),
        .item_size = sizeof(Mel_Swapchain_Entry), .initial_capacity = 4);
}
