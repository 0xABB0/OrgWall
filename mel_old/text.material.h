#pragma once

#include "math.vec4.h"

typedef struct {
    Mel_Vec4 color;
} Mel_Text_Atlas_Params;

typedef struct {
    Mel_Vec4 color;
    Mel_Vec4 outline_color;
    f32 edge;
    f32 softness;
    f32 outline;
    f32 px_range;
} Mel_Text_SDF_Params;

typedef struct {
    Mel_Vec4 color;
    Mel_Vec4 outline_color;
    f32 edge;
    f32 softness;
    f32 outline;
    f32 px_range;
} Mel_Text_MSDF_Params;

_Static_assert(sizeof(Mel_Text_Atlas_Params) == 16, "Mel_Text_Atlas_Params must be 16 bytes");
_Static_assert(sizeof(Mel_Text_SDF_Params) == 48, "Mel_Text_SDF_Params must be 48 bytes");
_Static_assert(sizeof(Mel_Text_MSDF_Params) == 48, "Mel_Text_MSDF_Params must be 48 bytes");
