#pragma once

#include "core.types.h"

typedef struct { u32 value; } Mel_Tileset_Handle;
#define MEL_TILESET_HANDLE_NULL ((Mel_Tileset_Handle){0})

typedef struct Mel_Tile_Source Mel_Tile_Source;
typedef struct Mel_Tile_Def Mel_Tile_Def;
typedef struct Mel_Tileset_Entry Mel_Tileset_Entry;
typedef struct Mel_Tileset_Pool Mel_Tileset_Pool;
