#pragma once

#include <core/types.h>
#include "curve.fwd.h"

#define MEL_CURVE_LINEAR  0
#define MEL_CURVE_STEPPED 1
#define MEL_CURVE_BEZIER  2

#define MEL_BEZIER_SEGMENTS 18

struct Mel_Bezier {
    f32 samples[MEL_BEZIER_SEGMENTS];
};

void mel_bezier_init(Mel_Bezier* bez, f32 cx1, f32 cy1, f32 cx2, f32 cy2);

#include "curve.inl"
