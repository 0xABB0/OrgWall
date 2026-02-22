#pragma once

#include "collection.slotmap.fwd.h"

typedef struct { Mel_SlotMap_Handle handle; } Mel_Font_Handle;
#define MEL_FONT_HANDLE_NULL ((Mel_Font_Handle){0})

typedef struct Mel_Font_Atlas_Entry Mel_Font_Atlas_Entry;
typedef struct Mel_Font_Atlas_Pool Mel_Font_Atlas_Pool;
