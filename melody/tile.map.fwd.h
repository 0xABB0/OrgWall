#pragma once

#include "types.h"

typedef struct { u32 value; } Mel_Tilemap_Handle;
#define MEL_TILEMAP_HANDLE_NULL ((Mel_Tilemap_Handle){0})

typedef struct Mel_Tilemap_Entry Mel_Tilemap_Entry;
typedef struct Mel_Tilemap_Pool Mel_Tilemap_Pool;
