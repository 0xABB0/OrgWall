#pragma once

#include "core.types.h"

typedef struct Mel_Track_Type_Def Mel_Track_Type_Def;

typedef void (*Mel_Batch_Lerp_Fn)(
    const void* restrict a,
    const void* restrict b,
    void* restrict out,
    const f32* restrict t,
    u32 count
);

typedef void (*Mel_Batch_Additive_Fn)(
    const void* restrict base,
    const void* restrict additive,
    const void* restrict reference,
    void* restrict out,
    const f32* restrict weight,
    u32 count
);
