#ifdef _CLANGD
#pragma once
#include "curve.h"
#endif

static inline f32 mel_curve_eval(u32 mode, f32 t, const Mel_Bezier* bez)
{
    if (mode == MEL_CURVE_LINEAR) return t;
    if (mode == MEL_CURVE_STEPPED) return 0.0f;

    assert(bez != NULL);

    const f32* s = bez->samples;
    i32 n = MEL_BEZIER_SEGMENTS / 2;

    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    i32 lo = 0, hi = n - 1;
    while (lo <= hi)
    {
        i32 mid = (lo + hi) / 2;
        if (s[mid * 2] < t) lo = mid + 1;
        else hi = mid - 1;
    }

    f32 x0, y0, x1, y1;
    if (lo == 0)
    {
        x0 = 0.0f; y0 = 0.0f;
        x1 = s[0]; y1 = s[1];
    }
    else if (lo >= n)
    {
        x0 = s[(n - 1) * 2]; y0 = s[(n - 1) * 2 + 1];
        x1 = 1.0f; y1 = 1.0f;
    }
    else
    {
        x0 = s[(lo - 1) * 2]; y0 = s[(lo - 1) * 2 + 1];
        x1 = s[lo * 2]; y1 = s[lo * 2 + 1];
    }

    f32 dx = x1 - x0;
    if (dx < 1e-7f) return y0;
    f32 frac = (t - x0) / dx;
    return y0 + (y1 - y0) * frac;
}
