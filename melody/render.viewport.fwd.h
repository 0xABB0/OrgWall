#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Render_View Mel_Render_View;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Render_View_Handle;
#define MEL_RENDER_VIEW_HANDLE_NULL ((Mel_Render_View_Handle){0})

static inline bool mel_render_view_handle_valid(Mel_Render_View_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
