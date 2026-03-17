#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Render_Manager Mel_Render_Manager;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Render_Handle;
#define MEL_RENDER_HANDLE_NULL ((Mel_Render_Handle){0})

static inline bool mel_render_handle_valid(Mel_Render_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
