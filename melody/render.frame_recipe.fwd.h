#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Frame_Recipe Mel_Frame_Recipe;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Frame_Recipe_Handle;
#define MEL_FRAME_RECIPE_HANDLE_NULL ((Mel_Frame_Recipe_Handle){0})

static inline bool mel_frame_recipe_handle_valid(Mel_Frame_Recipe_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
