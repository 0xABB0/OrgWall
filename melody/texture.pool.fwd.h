#pragma once

#include "types.h"

typedef struct { u32 value; } Mel_Texture_Handle;
#define MEL_TEXTURE_HANDLE_NULL ((Mel_Texture_Handle){0})

typedef struct Mel_Texture_Pool Mel_Texture_Pool;
