#include <test/test.h>

#include <pcm/convert.h>

#include <core/types.h>

MEL_TEST(pcm_convert, interleave_deinterleave_roundtrip)
{
    f32 left[8];
    f32 right[8];
    for (u32 i = 0; i < 8u; i++)
    {
        left[i] = (f32)i;
        right[i] = (f32)i - 100.f;
    }

    const f32* planar_in[2] = { left, right };
    f32        interleaved[16];
    mel_pcm_interleave(interleaved, planar_in, 2u, 8u);

    for (u32 i = 0; i < 8u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(interleaved[i * 2u + 0u], left[i], 0.0f);
        MEL_EXPECT_FLOAT_EQ(interleaved[i * 2u + 1u], right[i], 0.0f);
    }

    f32  out_left[8];
    f32  out_right[8];
    f32* planar_out[2] = { out_left, out_right };
    mel_pcm_deinterleave(planar_out, interleaved, 2u, 8u);

    for (u32 i = 0; i < 8u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out_left[i], left[i], 0.0f);
        MEL_EXPECT_FLOAT_EQ(out_right[i], right[i], 0.0f);
    }
}

MEL_TEST(pcm_convert, mono_interleave_is_copy)
{
    f32        src[4] = { 1.f, 2.f, 3.f, 4.f };
    const f32* planar[1] = { src };
    f32        dst[4];

    mel_pcm_interleave(dst, planar, 1u, 4u);
    for (u32 i = 0; i < 4u; i++)
        MEL_EXPECT_FLOAT_EQ(dst[i], src[i], 0.0f);
}

MEL_TEST(pcm_convert, zero_frames_is_noop)
{
    f32        plane[1] = { 3.f };
    const f32* planar[1] = { plane };
    f32        dst[1] = { 7.f };

    mel_pcm_interleave(dst, planar, 1u, 0u);
    MEL_EXPECT_FLOAT_EQ(dst[0], 7.f, 0.0f);
}

MEL_TEST(pcm_convert, i16_to_f32_known_values)
{
    i16 src[5] = { -32768, -16384, 0, 16384, 32767 };
    f32 dst[5];

    mel_pcm_i16_to_f32(dst, src, 5u);
    MEL_EXPECT_FLOAT_EQ(dst[0], -1.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[1], -0.5f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[2], 0.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[3], 0.5f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[4], 32767.0f / 32768.0f, 0.0f);
}

MEL_TEST(pcm_convert, f32_to_i16_clamps)
{
    f32 src[6] = { -2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f };
    i16 dst[6];

    mel_pcm_f32_to_i16(dst, src, 6u);
    MEL_EXPECT_EQ(dst[0], -32768);
    MEL_EXPECT_EQ(dst[1], -32768);
    MEL_EXPECT_EQ(dst[2], -16384);
    MEL_EXPECT_EQ(dst[3], 16384);
    MEL_EXPECT_EQ(dst[4], 32767);
    MEL_EXPECT_EQ(dst[5], 32767);
}

MEL_TEST(pcm_convert, i16_roundtrip_is_identity)
{
    u32 mismatched = 0;
    for (i32 s = -32768; s <= 32767; s++)
    {
        i16 in = (i16)s;
        f32 f;
        i16 out;
        mel_pcm_i16_to_f32(&f, &in, 1u);
        mel_pcm_f32_to_i16(&out, &f, 1u);
        if (out != in)
            mismatched++;
    }
    MEL_EXPECT_EQ(mismatched, 0u);
}
