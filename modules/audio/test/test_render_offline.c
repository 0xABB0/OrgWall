#include <test/test.h>

#include <audio/audio.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>

#include <math.h>

#define SR    48000u
#define CH    2u
#define BLOCK 256u

static const f32 MEL_SQRT1_2 = 0.70710678118654752440f;

static const Mel_Alloc* test_alloc(void) { return mel_alloc_heap(); }

static Mel_Audio_Opt base_opt(void)
{
    return (Mel_Audio_Opt){
        .samplerate = SR,
        .channels = CH,
        .block_frames = BLOCK,
        .ring_blocks = 2u,
        .master_volume = 1.0f,
        .resampler = NULL,
        .exec = NULL,
    };
}

static Mel_Audio_Source* mono_ramp(const Mel_Alloc* a, u32 frames)
{
    f32* buf = mel_alloc(a, sizeof(f32) * frames);
    for (u32 i = 0; i < frames; i++)
        buf[i] = (f32)i;
    Mel_Audio_Source* s = mel_audio_pcm_from_float(a, buf, frames, 1u, SR, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return s;
}

static Mel_Audio_Source* mono_const(const Mel_Alloc* a, u32 frames, f32 value)
{
    f32* buf = mel_alloc(a, sizeof(f32) * frames);
    for (u32 i = 0; i < frames; i++)
        buf[i] = value;
    Mel_Audio_Source* s = mel_audio_pcm_from_float(a, buf, frames, 1u, SR, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return s;
}

MEL_TEST(render, unity_gain_single_voice)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = mono_const(a, 64u, 1.0f);
    mel_audio_play_ex(eng, src, 1.0f, 0.0f, false);

    f32 out[64u * CH];
    u32 produced = mel_audio_render(eng, out, 64u);
    MEL_REQUIRE_EQ(produced, 64u);

    f32 center = MEL_SQRT1_2;
    for (u32 i = 0; i < 64u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], center, 1e-4f);
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 1u], center, 1e-4f);
    }

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(render, volume_scaling)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = mono_const(a, 32u, 1.0f);
    mel_audio_play_ex(eng, src, 0.25f, 0.0f, false);

    f32 out[32u * CH];
    mel_audio_render(eng, out, 32u);

    f32 expect = MEL_SQRT1_2 * 0.25f;
    for (u32 i = 0; i < 32u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], expect, 1e-4f);
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 1u], expect, 1e-4f);
    }

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(render, constant_power_pan)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* hard_left = mono_const(a, 16u, 1.0f);
    mel_audio_play_ex(eng, hard_left, 1.0f, -1.0f, false);

    f32 out[16u * CH];
    mel_audio_render(eng, out, 16u);

    for (u32 i = 0; i < 16u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], 1.0f, 1e-4f);
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 1u], 0.0f, 1e-4f);
    }

    mel_audio_destroy(eng);
    hard_left->source_free(hard_left, a);

    eng = mel_audio_create_offline(a, NULL, base_opt());
    Mel_Audio_Source* hard_right = mono_const(a, 16u, 1.0f);
    mel_audio_play_ex(eng, hard_right, 1.0f, 1.0f, false);
    mel_audio_render(eng, out, 16u);
    for (u32 i = 0; i < 16u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], 0.0f, 1e-4f);
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 1u], 1.0f, 1e-4f);
    }

    mel_audio_destroy(eng);
    hard_right->source_free(hard_right, a);
}

MEL_TEST(render, two_voice_mix)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* a_src = mono_const(a, 16u, 0.5f);
    Mel_Audio_Source* b_src = mono_const(a, 16u, 0.25f);
    mel_audio_play_ex(eng, a_src, 1.0f, -1.0f, false);
    mel_audio_play_ex(eng, b_src, 1.0f, 1.0f, false);

    f32 out[16u * CH];
    mel_audio_render(eng, out, 16u);

    for (u32 i = 0; i < 16u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], 0.5f, 1e-4f);
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 1u], 0.25f, 1e-4f);
    }

    mel_audio_destroy(eng);
    a_src->source_free(a_src, a);
    b_src->source_free(b_src, a);
}

MEL_TEST(render, loop_wrap)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    f32               pattern[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
    Mel_Audio_Source* src = mel_audio_pcm_from_float(a, pattern, 4u, 1u, SR, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_audio_pcm_set_loop(src, true, 0.0);
    mel_audio_play_ex(eng, src, 1.0f, -1.0f, false);

    f32 out[10u * CH];
    mel_audio_render(eng, out, 10u);

    for (u32 i = 0; i < 10u; i++)
    {
        f32 expect = pattern[i % 4u];
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], expect, 1e-4f);
    }

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(render, voice_ends_when_source_exhausted)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = mono_const(a, 8u, 1.0f);
    mel_audio_play_ex(eng, src, 1.0f, -1.0f, false);

    f32 out[20u * CH];
    mel_audio_render(eng, out, 20u);

    f32 unity = 1.0f;
    for (u32 i = 0; i < 8u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], unity, 1e-4f);
    for (u32 i = 8u; i < 20u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], 0.0f, 1e-4f);

    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(render, resample_double_speed)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = mono_ramp(a, 64u);
    Mel_Audio_Voice   v = mel_audio_play_ex(eng, src, 1.0f, -1.0f, false);
    mel_audio_set_play_speed(eng, v, 2.0);

    f32 out[16u * CH];
    mel_audio_render(eng, out, 16u);

    for (u32 i = 0; i < 16u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], (f32)(2u * i), 1e-3f);

    mel_audio_destroy(eng);
    src->source_free(src, a);
}
