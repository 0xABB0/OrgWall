#include "anim.track.h"
#include "math.curve.h"
#include "math.scalar.h"
#include "allocator.heap.h"
#include "test.harness.h"

MEL_TEST(f32_exact_keyframes)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 3);

    f32 v0 = 0.0f, v1 = 5.0f, v2 = 10.0f;
    mel_anim_track_set_keyframe(&track, 0, 0.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, &v1, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 2, 2.0f, &v2, MEL_CURVE_LINEAR);

    f32 out;
    mel_anim_track_eval(&track, 0.0f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 0.0f, 1e-6f);

    mel_anim_track_eval(&track, 1.0f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 5.0f, 1e-6f);

    mel_anim_track_eval(&track, 2.0f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 10.0f, 1e-6f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(f32_lerp_between_keyframes)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 2);

    f32 v0 = 0.0f, v1 = 10.0f;
    mel_anim_track_set_keyframe(&track, 0, 0.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, &v1, MEL_CURVE_LINEAR);

    f32 out;
    mel_anim_track_eval(&track, 0.5f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 5.0f, 1e-5f);

    mel_anim_track_eval(&track, 0.25f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 2.5f, 1e-5f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(f32_clamp_before_and_after)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 2);

    f32 v0 = 3.0f, v1 = 7.0f;
    mel_anim_track_set_keyframe(&track, 0, 1.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 2.0f, &v1, MEL_CURVE_LINEAR);

    f32 out;
    mel_anim_track_eval(&track, 0.0f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 3.0f, 1e-6f);

    mel_anim_track_eval(&track, 5.0f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 7.0f, 1e-6f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(stepped_curve)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 3);

    f32 v0 = 0.0f, v1 = 10.0f, v2 = 20.0f;
    mel_anim_track_set_keyframe(&track, 0, 0.0f, &v0, MEL_CURVE_STEPPED);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, &v1, MEL_CURVE_STEPPED);
    mel_anim_track_set_keyframe(&track, 2, 2.0f, &v2, MEL_CURVE_LINEAR);

    f32 out;
    mel_anim_track_eval(&track, 0.5f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 0.0f, 1e-6f);

    mel_anim_track_eval(&track, 1.5f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 10.0f, 1e-6f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(bezier_curve)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 2);

    f32 v0 = 0.0f, v1 = 100.0f;
    mel_anim_track_set_keyframe(&track, 0, 0.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, &v1, MEL_CURVE_LINEAR);

    mel_anim_track_set_bezier(&track, alloc, 0, 0.42f, 0.0f, 0.58f, 1.0f);

    f32 out;
    mel_anim_track_eval(&track, 0.5f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 50.0f, 5.0f);
    MEL_ASSERT(out > 0.0f && out < 100.0f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(vec2_lerp)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_VEC2, 2);

    f32 v0[] = {0.0f, 0.0f};
    f32 v1[] = {10.0f, 20.0f};
    mel_anim_track_set_keyframe(&track, 0, 0.0f, v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, v1, MEL_CURVE_LINEAR);

    f32 out[2];
    mel_anim_track_eval(&track, 0.5f, out);
    MEL_ASSERT_FLOAT_EQ(out[0], 5.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(out[1], 10.0f, 1e-5f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(vec4_lerp)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_VEC4, 2);

    f32 v0[] = {1.0f, 0.0f, 0.0f, 1.0f};
    f32 v1[] = {0.0f, 0.0f, 1.0f, 1.0f};
    mel_anim_track_set_keyframe(&track, 0, 0.0f, v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, v1, MEL_CURVE_LINEAR);

    f32 out[4];
    mel_anim_track_eval(&track, 0.5f, out);
    MEL_ASSERT_FLOAT_EQ(out[0], 0.5f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(out[1], 0.0f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(out[2], 0.5f, 1e-5f);
    MEL_ASSERT_FLOAT_EQ(out[3], 1.0f, 1e-5f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(find_keyframe_binary_search)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 5);

    f32 v = 0.0f;
    mel_anim_track_set_keyframe(&track, 0, 0.0f, &v, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 0.25f, &v, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 2, 0.5f, &v, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 3, 0.75f, &v, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 4, 1.0f, &v, MEL_CURVE_LINEAR);

    MEL_ASSERT_EQ(mel_anim_track_find_keyframe(&track, 0.0f), 0u);
    MEL_ASSERT_EQ(mel_anim_track_find_keyframe(&track, 0.1f), 0u);
    MEL_ASSERT_EQ(mel_anim_track_find_keyframe(&track, 0.25f), 1u);
    MEL_ASSERT_EQ(mel_anim_track_find_keyframe(&track, 0.3f), 1u);
    MEL_ASSERT_EQ(mel_anim_track_find_keyframe(&track, 0.5f), 2u);
    MEL_ASSERT_EQ(mel_anim_track_find_keyframe(&track, 0.6f), 2u);
    MEL_ASSERT_EQ(mel_anim_track_find_keyframe(&track, 1.0f), 4u);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(single_keyframe)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 1);

    f32 v = 42.0f;
    mel_anim_track_set_keyframe(&track, 0, 0.0f, &v, MEL_CURVE_LINEAR);

    f32 out;
    mel_anim_track_eval(&track, 0.0f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 42.0f, 1e-6f);

    mel_anim_track_eval(&track, 100.0f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 42.0f, 1e-6f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(angle_blend_shortest_path)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 2);
    track.blend = mel_blend_angle;

    f32 v0 = 350.0f * (MEL_PI / 180.0f);
    f32 v1 = 30.0f  * (MEL_PI / 180.0f);
    mel_anim_track_set_keyframe(&track, 0, 0.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, &v1, MEL_CURVE_LINEAR);

    f32 out;
    mel_anim_track_eval(&track, 0.5f, &out);

    f32 naive_lerp = v0 + (v1 - v0) * 0.5f;
    f32 diff_from_naive = mel_absf(mel_angle_diff(out, naive_lerp));
    MEL_ASSERT(diff_from_naive > 1.0f);

    f32 expected = 10.0f * (MEL_PI / 180.0f);
    f32 diff_from_expected = mel_absf(mel_angle_diff(out, expected));
    MEL_ASSERT(diff_from_expected < 0.02f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(null_blend_uses_default_lerp)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_F32, 2);

    MEL_ASSERT_NULL(track.blend);

    f32 v0 = 0.0f, v1 = 10.0f;
    mel_anim_track_set_keyframe(&track, 0, 0.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, &v1, MEL_CURVE_LINEAR);

    f32 out;
    mel_anim_track_eval(&track, 0.5f, &out);
    MEL_ASSERT_FLOAT_EQ(out, 5.0f, 1e-5f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(quat_slerp_basic)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_VEC4, 2);
    track.blend = mel_blend_quat_slerp;

    f32 identity[] = {0.0f, 0.0f, 0.0f, 1.0f};
    f32 rot90z[] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    mel_anim_track_set_keyframe(&track, 0, 0.0f, identity, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, rot90z, MEL_CURVE_LINEAR);

    f32 out[4];
    mel_anim_track_eval(&track, 0.5f, out);

    f32 expected_x = 0.0f;
    f32 expected_y = 0.0f;
    f32 expected_z = 0.38268343f;
    f32 expected_w = 0.92387953f;

    MEL_ASSERT_FLOAT_EQ(out[0], expected_x, 1e-4f);
    MEL_ASSERT_FLOAT_EQ(out[1], expected_y, 1e-4f);
    MEL_ASSERT_FLOAT_EQ(out[2], expected_z, 1e-3f);
    MEL_ASSERT_FLOAT_EQ(out[3], expected_w, 1e-3f);

    f32 len = mel_sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
    MEL_ASSERT_FLOAT_EQ(len, 1.0f, 1e-5f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

MEL_TEST(quat_slerp_antipodal)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Track track;
    mel_anim_track_init(&track, alloc, 1, MEL_ANIM_TRACK_VEC4, 2);
    track.blend = mel_blend_quat_slerp;

    f32 identity[] = {0.0f, 0.0f, 0.0f, 1.0f};
    f32 rot90z_neg[] = {0.0f, 0.0f, -0.70710678f, -0.70710678f};
    mel_anim_track_set_keyframe(&track, 0, 0.0f, identity, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(&track, 1, 1.0f, rot90z_neg, MEL_CURVE_LINEAR);

    f32 out[4];
    mel_anim_track_eval(&track, 0.5f, out);

    f32 expected_z = 0.38268343f;
    f32 expected_w = 0.92387953f;

    MEL_ASSERT_FLOAT_EQ(out[0], 0.0f, 1e-4f);
    MEL_ASSERT_FLOAT_EQ(out[1], 0.0f, 1e-4f);
    MEL_ASSERT_FLOAT_EQ(out[2], expected_z, 1e-3f);
    MEL_ASSERT_FLOAT_EQ(out[3], expected_w, 1e-3f);

    mel_anim_track_destroy(&track, alloc);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("anim.track");

    MEL_RUN_TEST(f32_exact_keyframes);
    MEL_RUN_TEST(f32_lerp_between_keyframes);
    MEL_RUN_TEST(f32_clamp_before_and_after);
    MEL_RUN_TEST(stepped_curve);
    MEL_RUN_TEST(bezier_curve);
    MEL_RUN_TEST(vec2_lerp);
    MEL_RUN_TEST(vec4_lerp);
    MEL_RUN_TEST(find_keyframe_binary_search);
    MEL_RUN_TEST(single_keyframe);
    MEL_RUN_TEST(angle_blend_shortest_path);
    MEL_RUN_TEST(null_blend_uses_default_lerp);
    MEL_RUN_TEST(quat_slerp_basic);
    MEL_RUN_TEST(quat_slerp_antipodal);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
