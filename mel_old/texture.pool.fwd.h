#pragma once

#include "collection.slotmap.fwd.h"

typedef struct { Mel_SlotMap_Handle handle; } Mel_Texture_Handle;
#define MEL_TEXTURE_HANDLE_NULL ((Mel_Texture_Handle){0})

typedef struct Mel_Texture_Pool Mel_Texture_Pool;

static inline u16 mel_texture_bucket(Mel_Texture_Handle h)
{
    return (u16)h.handle.index;
}
