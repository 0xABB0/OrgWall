#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Frame_Plan Mel_Frame_Plan;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Frame_Plan_Handle;
#define MEL_FRAME_PLAN_HANDLE_NULL ((Mel_Frame_Plan_Handle){0})

static inline bool mel_frame_plan_handle_valid(Mel_Frame_Plan_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
