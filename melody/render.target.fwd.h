#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Render_Target Mel_Render_Target;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Render_Target_Handle;
#define MEL_RENDER_TARGET_HANDLE_NULL ((Mel_Render_Target_Handle){0})

static inline bool mel_render_target_handle_valid(Mel_Render_Target_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
