#include <math/curve.h>

void mel_bezier_init(Mel_Bezier* bez, f32 cx1, f32 cy1, f32 cx2, f32 cy2)
{
    assert(bez != NULL);

    i32 n = MEL_BEZIER_SEGMENTS / 2;
    f32 h = 1.0f / (f32)(n + 1);
    f32 h2 = h * h;
    f32 h3 = h2 * h;

    f32 ax = 3.0f * cx1 - 3.0f * cx2 + 1.0f;
    f32 bx = -6.0f * cx1 + 3.0f * cx2;
    f32 cx_ = 3.0f * cx1;

    f32 ay = 3.0f * cy1 - 3.0f * cy2 + 1.0f;
    f32 by = -6.0f * cy1 + 3.0f * cy2;
    f32 cy_ = 3.0f * cy1;

    f32 dx = ax * h3 + bx * h2 + cx_ * h;
    f32 ddx = 6.0f * ax * h3 + 2.0f * bx * h2;
    f32 dddx = 6.0f * ax * h3;

    f32 dy = ay * h3 + by * h2 + cy_ * h;
    f32 ddy = 6.0f * ay * h3 + 2.0f * by * h2;
    f32 dddy = 6.0f * ay * h3;

    f32 x = 0.0f, y = 0.0f;

    for (i32 i = 0; i < n; i++)
    {
        x += dx; dx += ddx; ddx += dddx;
        y += dy; dy += ddy; ddy += dddy;
        bez->samples[i * 2] = x;
        bez->samples[i * 2 + 1] = y;
    }
}
