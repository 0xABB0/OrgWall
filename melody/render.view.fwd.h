#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_View Mel_View;
typedef struct { Mel_SlotMap_Handle handle; } Mel_View_Handle;
#define MEL_VIEW_HANDLE_NULL ((Mel_View_Handle){0})

static inline bool mel_view_handle_valid(Mel_View_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
