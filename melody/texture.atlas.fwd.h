#pragma once

#include "types.h"

typedef struct { u32 value; } Mel_Atlas_Handle;
#define MEL_ATLAS_HANDLE_NULL ((Mel_Atlas_Handle){0})

typedef struct Mel_Atlas_Pool Mel_Atlas_Pool;
typedef struct Mel_Atlas_Entry Mel_Atlas_Entry;
typedef struct Mel_Atlas_Region Mel_Atlas_Region;
