#pragma once

#include "collection.slotmap.fwd.h"

typedef struct Mel_Geometry_Pool Mel_Geometry_Pool;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Geometry_Handle;
#define MEL_GEOMETRY_HANDLE_NULL ((Mel_Geometry_Handle){0})

static inline bool mel_geometry_handle_valid(Mel_Geometry_Handle h)
{
    return mel_slotmap_handle_valid(h.handle);
}
