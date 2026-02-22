#pragma once

#include "collection.slotmap.fwd.h"

typedef struct { Mel_SlotMap_Handle handle; } Mel_Atlas_Handle;
#define MEL_ATLAS_HANDLE_NULL ((Mel_Atlas_Handle){0})

typedef struct Mel_Atlas_Pool Mel_Atlas_Pool;
typedef struct Mel_Atlas_Entry Mel_Atlas_Entry;
typedef struct Mel_Atlas_Region Mel_Atlas_Region;
