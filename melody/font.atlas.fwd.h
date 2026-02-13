#pragma once

#include "core.types.h"

typedef struct { u32 value; } Mel_Font_Handle;
#define MEL_FONT_HANDLE_NULL ((Mel_Font_Handle){0})

typedef struct Mel_Font_Atlas_Entry Mel_Font_Atlas_Entry;
typedef struct Mel_Font_Atlas_Pool Mel_Font_Atlas_Pool;
