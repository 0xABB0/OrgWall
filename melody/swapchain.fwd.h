#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Swapchain Mel_Swapchain;
typedef struct Mel_Swapchain_Vtable Mel_Swapchain_Vtable;
typedef struct Mel_Swapchain_Entry Mel_Swapchain_Entry;

typedef struct { Mel_SlotMap_Handle handle; } Mel_Swapchain_Handle;
#define MEL_SWAPCHAIN_HANDLE_NULL ((Mel_Swapchain_Handle){0})

static inline bool mel_swapchain_handle_valid(Mel_Swapchain_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
