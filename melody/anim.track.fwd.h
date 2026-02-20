#pragma once

#include "core.types.h"

typedef struct Mel_Anim_Track Mel_Anim_Track;
typedef void (*Mel_Blend_Fn)(f32* out, const f32* a, const f32* b, u32 stride, f32 t);
