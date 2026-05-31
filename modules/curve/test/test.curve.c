#include <curve/curve.h>
#include <test/test.h>

#define EPS 1e-4f

MEL_TEST(curve, linear_is_identity)
{
    for (int i = 0; i <= 10; ++i)
    {
        f32 t = (f32)i / 10.0f;
        MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_LINEAR, t, NULL), t, EPS);
    }
}

MEL_TEST(curve, stepped_holds_zero)
{
    MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_STEPPED, 0.0f, NULL), 0.0f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_STEPPED, 0.5f, NULL), 0.0f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_STEPPED, 1.0f, NULL), 0.0f, EPS);
}

MEL_TEST(curve, diagonal_bezier_is_identity)
{
    Mel_Bezier bez;
    mel_bezier_init(&bez, 1.0f / 3.0f, 1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f);
    for (int i = 1; i < 10; ++i)
    {
        f32 t = (f32)i / 10.0f;
        MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, t, &bez), t, 1e-3f);
    }
}

MEL_TEST(curve, bezier_pins_endpoints)
{
    Mel_Bezier bez;
    mel_bezier_init(&bez, 0.42f, 0.0f, 0.58f, 1.0f);
    MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, 0.0f, &bez), 0.0f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, 1.0f, &bez), 1.0f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, -0.5f, &bez), 0.0f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, 1.5f, &bez), 1.0f, EPS);
}

MEL_TEST(curve, bezier_is_monotone_and_bounded)
{
    Mel_Bezier bez;
    mel_bezier_init(&bez, 0.42f, 0.0f, 0.58f, 1.0f);
    f32 prev = 0.0f;
    for (int i = 0; i <= 100; ++i)
    {
        f32 v = mel_curve_eval(MEL_CURVE_BEZIER, (f32)i / 100.0f, &bez);
        MEL_EXPECT_GE(v, -EPS);
        MEL_EXPECT_LE(v, 1.0f + EPS);
        MEL_EXPECT_GE(v, prev - EPS);
        prev = v;
    }
}
