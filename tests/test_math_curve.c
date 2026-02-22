#include "math.curve.h"
#include "test.harness.h"

MEL_TEST(linear_returns_t, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_LINEAR, 0.0f, NULL), 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_LINEAR, 0.5f, NULL), 0.5f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_LINEAR, 1.0f, NULL), 1.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_LINEAR, 0.25f, NULL), 0.25f, 1e-6f);
}

MEL_TEST(stepped_returns_zero, .tags = "math")
{
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_STEPPED, 0.0f, NULL), 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_STEPPED, 0.5f, NULL), 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_STEPPED, 1.0f, NULL), 0.0f, 1e-6f);
}

MEL_TEST(bezier_ease_s_curve, .tags = "math")
{
    Mel_Bezier bez;
    mel_bezier_init(&bez, 0.42f, 0.0f, 0.58f, 1.0f);

    f32 at_quarter = mel_curve_eval(MEL_CURVE_BEZIER, 0.25f, &bez);
    f32 at_half = mel_curve_eval(MEL_CURVE_BEZIER, 0.5f, &bez);
    f32 at_three_quarter = mel_curve_eval(MEL_CURVE_BEZIER, 0.75f, &bez);

    MEL_ASSERT(at_quarter < 0.25f);
    MEL_ASSERT_FLOAT_EQ(at_half, 0.5f, 0.05f);
    MEL_ASSERT(at_three_quarter > 0.75f);
}

MEL_TEST(bezier_linear_approximation, .tags = "math")
{
    Mel_Bezier bez;
    mel_bezier_init(&bez, 0.0f, 0.0f, 1.0f, 1.0f);

    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, 0.25f, &bez), 0.25f, 0.02f);
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, 0.5f, &bez), 0.5f, 0.02f);
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, 0.75f, &bez), 0.75f, 0.02f);
}

MEL_TEST(bezier_edge_cases, .tags = "math")
{
    Mel_Bezier bez;
    mel_bezier_init(&bez, 0.25f, 0.1f, 0.25f, 1.0f);

    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, 0.0f, &bez), 0.0f, 1e-6f);
    MEL_ASSERT_FLOAT_EQ(mel_curve_eval(MEL_CURVE_BEZIER, 1.0f, &bez), 1.0f, 1e-6f);
}