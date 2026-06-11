#include <test/test.h>

#include <pcm/resample.h>

#include <core/types.h>

MEL_TEST(pcm_resample, unity_ratio_is_identity)
{
    f32 src[8] = { 0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f };
    f32 dst[8];
    f64 cursor = 0.0;

    u32 out = mel_pcm_resample_linear(src, 8u, dst, 8u, 1.0, &cursor);
    MEL_EXPECT_EQ(out, 8u);
    for (u32 i = 0; i < 8u; i++)
        MEL_EXPECT_FLOAT_EQ(dst[i], src[i], 0.0f);
    MEL_EXPECT_FLOAT_EQ(cursor, 8.0, 0.0);
}

MEL_TEST(pcm_resample, upsample_interpolates_midpoints)
{
    f32 src[4] = { 0.f, 2.f, 4.f, 6.f };
    f32 dst[7];
    f64 cursor = 0.0;

    u32 out = mel_pcm_resample_linear(src, 4u, dst, 7u, 0.5, &cursor);
    MEL_EXPECT_EQ(out, 7u);
    f32 expect[7] = { 0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f };
    for (u32 i = 0; i < 7u; i++)
        MEL_EXPECT_FLOAT_EQ(dst[i], expect[i], 1e-6f);
}

MEL_TEST(pcm_resample, downsample_skips_frames)
{
    f32 src[8] = { 0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f };
    f32 dst[4];
    f64 cursor = 0.0;

    u32 out = mel_pcm_resample_linear(src, 8u, dst, 4u, 2.0, &cursor);
    MEL_EXPECT_EQ(out, 4u);
    f32 expect[4] = { 0.f, 2.f, 4.f, 6.f };
    for (u32 i = 0; i < 4u; i++)
        MEL_EXPECT_FLOAT_EQ(dst[i], expect[i], 1e-6f);
}

MEL_TEST(pcm_resample, cursor_carries_across_calls)
{
    f32 src[16];
    for (u32 i = 0; i < 16u; i++)
        src[i] = (f32)i * (f32)i;

    const f64 ratio = 44100.0 / 48000.0;

    f32 whole[12];
    f64 cursor_whole = 0.0;
    MEL_EXPECT_EQ(mel_pcm_resample_linear(src, 16u, whole, 12u, ratio, &cursor_whole), 12u);

    f32 split[12];
    f64 cursor_split = 0.0;
    MEL_EXPECT_EQ(mel_pcm_resample_linear(src, 16u, split, 5u, ratio, &cursor_split), 5u);
    MEL_EXPECT_EQ(mel_pcm_resample_linear(src, 16u, split + 5u, 7u, ratio, &cursor_split), 7u);

    for (u32 i = 0; i < 12u; i++)
        MEL_EXPECT_FLOAT_EQ(split[i], whole[i], 0.0f);
    MEL_EXPECT_FLOAT_EQ(cursor_split, cursor_whole, 0.0);
}

MEL_TEST(pcm_resample, past_end_clamps_to_last_frame)
{
    f32 src[4] = { 1.f, 2.f, 3.f, 9.f };
    f32 dst[6];
    f64 cursor = 0.0;

    u32 out = mel_pcm_resample_linear(src, 4u, dst, 6u, 1.0, &cursor);
    MEL_EXPECT_EQ(out, 6u);
    MEL_EXPECT_FLOAT_EQ(dst[3], 9.f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[4], 9.f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[5], 9.f, 0.0f);
}

MEL_TEST(pcm_resample, empty_source_produces_silence)
{
    f32 src[1] = { 5.f };
    f32 dst[4] = { 1.f, 1.f, 1.f, 1.f };
    f64 cursor = 0.0;

    u32 out = mel_pcm_resample_linear(src, 0u, dst, 4u, 1.0, &cursor);
    MEL_EXPECT_EQ(out, 4u);
    for (u32 i = 0; i < 4u; i++)
        MEL_EXPECT_FLOAT_EQ(dst[i], 0.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(cursor, 0.0, 0.0);
}

MEL_TEST(pcm_resample, zero_output_is_noop)
{
    f32 src[4] = { 1.f, 2.f, 3.f, 4.f };
    f32 dst[1] = { 7.f };
    f64 cursor = 1.5;

    u32 out = mel_pcm_resample_linear(src, 4u, dst, 0u, 1.0, &cursor);
    MEL_EXPECT_EQ(out, 0u);
    MEL_EXPECT_FLOAT_EQ(dst[0], 7.f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(cursor, 1.5, 0.0);
}
