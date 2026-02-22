#pragma once

#include "collection.slotmap.fwd.h"

typedef struct { Mel_SlotMap_Handle handle; } Mel_Tileset_Handle;
#define MEL_TILESET_HANDLE_NULL ((Mel_Tileset_Handle){0})

typedef struct Mel_Tile_Source Mel_Tile_Source;
typedef struct Mel_Tile_Def Mel_Tile_Def;
typedef struct Mel_Tileset_Entry Mel_Tileset_Entry;
typedef struct Mel_Tileset_Pool Mel_Tileset_Pool;
