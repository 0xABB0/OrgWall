#pragma once

#include <collection.slotmap/slotmap.fwd.h>

typedef struct { Mel_SlotMap_Handle handle; } Mel_Font_SDF_Handle;
#define MEL_FONT_SDF_HANDLE_NULL ((Mel_Font_SDF_Handle){0})

typedef struct Mel_Font_SDF_Entry Mel_Font_SDF_Entry;
