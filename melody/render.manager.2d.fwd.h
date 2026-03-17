#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Render_Manager_2D Mel_Render_Manager_2D;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Render_Handle_2D;
#define MEL_RENDER_HANDLE_2D_NULL ((Mel_Render_Handle_2D){0})

static inline bool mel_render_handle_2d_valid(Mel_Render_Handle_2D h)
{
    return mel_slotmap_handle_valid(h.handle);
}
