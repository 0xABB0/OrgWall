#include <easing/easing.h>
#include <test/test.h>

#define EPS 1e-5f

static const Mel_Easing_Entry mel__easings[] = {
#define X(n, f) { n, f },
    MEL_EASING_LIST(X)
#undef X
};

MEL_TEST(easing, registry_count_matches_list) { MEL_EXPECT_EQ((int)(sizeof(mel__easings) / sizeof(mel__easings[0])), MEL_EASING_COUNT); }

MEL_TEST(easing, endpoints_are_pinned)
{
    for (int i = 0; i < MEL_EASING_COUNT; ++i)
    {
        Mel_Easing_Entry e = mel__easings[i];
        if (strcmp(e.name, "step") == 0)
            continue;
        MEL_EXPECT_FLOAT_EQ(e.func(0.0f), 0.0f, EPS);
        MEL_EXPECT_FLOAT_EQ(e.func(1.0f), 1.0f, EPS);
    }
}

MEL_TEST(easing, linear_is_identity)
{
    for (int i = 0; i <= 10; ++i)
    {
        f32 t = (f32)i / 10.0f;
        MEL_EXPECT_FLOAT_EQ(mel_ease_linear(t), t, EPS);
    }
}

MEL_TEST(easing, out_is_reflected_in)
{
    for (int i = 0; i <= 10; ++i)
    {
        f32 t = (f32)i / 10.0f;
        MEL_EXPECT_FLOAT_EQ(mel_ease_out_quad(t), 1.0f - mel_ease_in_quad(1.0f - t), EPS);
        MEL_EXPECT_FLOAT_EQ(mel_ease_out_cubic(t), 1.0f - mel_ease_in_cubic(1.0f - t), EPS);
        MEL_EXPECT_FLOAT_EQ(mel_ease_out_quart(t), 1.0f - mel_ease_in_quart(1.0f - t), EPS);
        MEL_EXPECT_FLOAT_EQ(mel_ease_out_quint(t), 1.0f - mel_ease_in_quint(1.0f - t), EPS);
        MEL_EXPECT_FLOAT_EQ(mel_ease_out_bounce(t), 1.0f - mel_ease_in_bounce(1.0f - t), EPS);
    }
}

MEL_TEST(easing, monotone_families_stay_bounded)
{
    Mel_Easing_Func monotone[] = {
        mel_ease_in_quad,     mel_ease_out_quad, mel_ease_in_out_quad, mel_ease_in_cubic,    mel_ease_out_cubic, mel_ease_in_out_cubic, mel_ease_in_sine,     mel_ease_out_sine,
        mel_ease_in_out_sine, mel_ease_in_circ,  mel_ease_out_circ,    mel_ease_in_out_circ, mel_ease_in_expo,   mel_ease_out_expo,     mel_ease_in_out_expo,
    };
    for (size_t k = 0; k < sizeof(monotone) / sizeof(monotone[0]); ++k)
        for (int i = 0; i <= 100; ++i)
        {
            f32 v = monotone[k]((f32)i / 100.0f);
            MEL_EXPECT_GE(v, -EPS);
            MEL_EXPECT_LE(v, 1.0f + EPS);
        }
}

MEL_TEST(easing, in_out_meets_at_midpoint)
{
    MEL_EXPECT_FLOAT_EQ(mel_ease_in_out_quad(0.5f), 0.5f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_ease_in_out_cubic(0.5f), 0.5f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_ease_in_out_sine(0.5f), 0.5f, EPS);
}

MEL_TEST(easing, step_holds_zero)
{
    MEL_EXPECT_FLOAT_EQ(mel_ease_step(0.0f), 0.0f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_ease_step(0.5f), 0.0f, EPS);
    MEL_EXPECT_FLOAT_EQ(mel_ease_step(1.0f), 0.0f, EPS);
}
