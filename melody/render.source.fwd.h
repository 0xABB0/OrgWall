#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Source Mel_Source;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Source_Handle;
#define MEL_SOURCE_HANDLE_NULL ((Mel_Source_Handle){0})

static inline bool mel_source_handle_valid(Mel_Source_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
