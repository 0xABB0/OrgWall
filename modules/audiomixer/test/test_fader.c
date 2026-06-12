#include <test/test.h>

#include <audiomixer/audiomixer.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>

#include <math.h>

#define SR    48000u
#define CH    2u
#define BLOCK 256u
#define STEP  480u

static const f32 MEL_SQRT1_2 = 0.70710678118654752440f;

static const Mel_Alloc* test_alloc(void) { return mel_alloc_heap(); }

static Mel_Mixer_Opt base_opt(void)
{
    return (Mel_Mixer_Opt){
        .samplerate = SR,
        .channels = CH,
        .block_frames = BLOCK,
        .ring_blocks = 2u,
        .master_volume = 1.0f,
        .resampler = NULL,
        .exec = NULL,
    };
}

static Mel_Mixer_Source* mono_const(const Mel_Alloc* a, u32 frames, f32 value)
{
    f32* buf = mel_alloc(a, sizeof(f32) * frames);
    for (u32 i = 0; i < frames; i++)
        buf[i] = value;
    Mel_Mixer_Source* s = mel_mixer_pcm_from_float(a, buf, frames, 1u, SR, MEL_MIXER_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return s;
}

static f32 first_left(Mel_Mixer* eng, f32* scratch)
{
    mel_mixer_render(eng, scratch, STEP);
    return scratch[0];
}

MEL_TEST(fader, fade_volume_reaches_target_on_time)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = mono_const(a, 64u * STEP, 1.0f);
    Mel_Mixer_Voice   v = mel_mixer_play_ex(eng, src, 1.0f, 0.0f, false);

    const f32 center = MEL_SQRT1_2;
    const u32 dur_frames = 4u * STEP;
    const f64 seconds = (f64)dur_frames / (f64)SR;
    mel_mixer_fade_volume(eng, v, 0.0f, seconds);

    f32 out[STEP * CH];
    for (u32 k = 0; k <= 4u; k++)
    {
        f32 got = first_left(eng, out);
        f64 clock = (f64)(k * STEP);
        f32 expect_vol = clock >= (f64)dur_frames ? 0.0f : (f32)(1.0 - clock / (f64)dur_frames);
        MEL_EXPECT_FLOAT_EQ(got, expect_vol * center, 1e-4f);
    }

    f32 tail = first_left(eng, out);
    MEL_EXPECT_FLOAT_EQ(tail, 0.0f, 1e-6f);

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(fader, fade_pan_reaches_target_on_time)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = mono_const(a, 64u * STEP, 1.0f);
    Mel_Mixer_Voice   v = mel_mixer_play_ex(eng, src, 1.0f, -1.0f, false);

    f32 out[STEP * CH];
    mel_mixer_render(eng, out, STEP);
    MEL_EXPECT_FLOAT_EQ(out[0], 1.0f, 1e-4f);
    MEL_EXPECT_FLOAT_EQ(out[1], 0.0f, 1e-4f);

    const u32 dur_frames = 4u * STEP;
    const f64 seconds = (f64)dur_frames / (f64)SR;
    mel_mixer_fade_pan(eng, v, 1.0f, seconds);

    for (u32 k = 0; k < 4u; k++)
        mel_mixer_render(eng, out, STEP);

    mel_mixer_render(eng, out, STEP);
    MEL_EXPECT_FLOAT_EQ(out[0], 0.0f, 1e-4f);
    MEL_EXPECT_FLOAT_EQ(out[1], 1.0f, 1e-4f);

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(fader, oscillate_volume_traverses_extremes)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = mono_const(a, 64u * STEP, 1.0f);
    Mel_Mixer_Voice   v = mel_mixer_play_ex(eng, src, 1.0f, 0.0f, false);

    const f32 center = MEL_SQRT1_2;
    const u32 period_frames = 8u * STEP;
    const f64 period = (f64)period_frames / (f64)SR;
    mel_mixer_oscillate_volume(eng, v, 0.0f, 1.0f, period);

    f32 out[STEP * CH];
    f32 vols[9];
    for (u32 k = 0; k <= 8u; k++)
    {
        mel_mixer_render(eng, out, STEP);
        vols[k] = out[0] / center;
    }

    MEL_EXPECT_FLOAT_EQ(vols[0], 0.0f, 1e-3f);
    MEL_EXPECT_FLOAT_EQ(vols[4], 1.0f, 1e-3f);
    MEL_EXPECT_FLOAT_EQ(vols[8], 0.0f, 1e-3f);
    MEL_EXPECT(vols[2] > 0.4f && vols[2] < 0.6f);
    MEL_EXPECT(vols[6] > 0.4f && vols[6] < 0.6f);

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(fader, schedule_stop_silences_voice_on_time)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = mono_const(a, 64u * STEP, 1.0f);
    Mel_Mixer_Voice   v = mel_mixer_play_ex(eng, src, 1.0f, 0.0f, false);

    const u32 dur_frames = 2u * STEP;
    const f64 seconds = (f64)dur_frames / (f64)SR;
    mel_mixer_schedule_stop(eng, v, seconds);

    f32 out[STEP * CH];
    mel_mixer_render(eng, out, STEP);
    MEL_EXPECT_GT(out[0], 0.0f);

    mel_mixer_render(eng, out, STEP);
    mel_mixer_render(eng, out, STEP);

    for (u32 i = 0; i < STEP; i++)
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], 0.0f, 1e-6f);

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(fader, schedule_pause_freezes_voice_on_time)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = mono_const(a, 64u * STEP, 1.0f);
    Mel_Mixer_Voice   v = mel_mixer_play_ex(eng, src, 1.0f, 0.0f, false);

    const u32 dur_frames = 2u * STEP;
    const f64 seconds = (f64)dur_frames / (f64)SR;
    mel_mixer_schedule_pause(eng, v, seconds);

    f32 out[STEP * CH];
    mel_mixer_render(eng, out, STEP);
    mel_mixer_render(eng, out, STEP);
    mel_mixer_render(eng, out, STEP);

    for (u32 i = 0; i < STEP; i++)
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], 0.0f, 1e-6f);

    MEL_EXPECT_EQ(mel_mixer_active_voice_count(eng), 1u);

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(fader, fade_master_volume_reaches_target)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = mono_const(a, 64u * STEP, 1.0f);
    mel_mixer_play_ex(eng, src, 1.0f, 0.0f, false);

    const f32 center = MEL_SQRT1_2;
    const u32 dur_frames = 4u * STEP;
    const f64 seconds = (f64)dur_frames / (f64)SR;
    mel_mixer_fade_master_volume(eng, 0.0f, seconds);

    f32 out[STEP * CH];
    for (u32 k = 0; k <= 4u; k++)
    {
        mel_mixer_render(eng, out, STEP);
        f64 clock = (f64)(k * STEP);
        f32 expect_master = clock >= (f64)dur_frames ? 0.0f : (f32)(1.0 - clock / (f64)dur_frames);
        MEL_EXPECT_FLOAT_EQ(out[0], expect_master * center, 1e-4f);
    }

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}
