#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Render_Environment Mel_Render_Environment;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Render_Environment_Handle;
#define MEL_RENDER_ENVIRONMENT_HANDLE_NULL ((Mel_Render_Environment_Handle){0})

static inline bool mel_render_environment_handle_valid(Mel_Render_Environment_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
