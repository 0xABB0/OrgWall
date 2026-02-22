#pragma once

#include "collection.slotmap.fwd.h"

typedef struct { Mel_SlotMap_Handle handle; } Mel_Tilemap_Handle;
#define MEL_TILEMAP_HANDLE_NULL ((Mel_Tilemap_Handle){0})

typedef struct Mel_Tilemap_Entry Mel_Tilemap_Entry;
typedef struct Mel_Tilemap_Pool Mel_Tilemap_Pool;
