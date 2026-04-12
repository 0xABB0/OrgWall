#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Window Mel_Window;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Window_Handle;
#define MEL_WINDOW_HANDLE_NULL ((Mel_Window_Handle){0})

static inline bool mel_window_handle_valid(Mel_Window_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
