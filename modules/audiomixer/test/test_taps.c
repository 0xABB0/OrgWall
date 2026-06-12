#include <test/test.h>

#include <audiomixer/audiomixer.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>

#include <string.h>

#define SR         48000u
#define CH         2u
#define BLOCK      128u

#define PAN_CENTER 0.70710678f

static Mel_Mixer_Opt opt(void)
{
    return (Mel_Mixer_Opt){
        .samplerate = SR,
        .channels = CH,
        .block_frames = BLOCK,
        .master_volume = 1.0f,
        .max_voice_channels = CH,
        .max_voice_ratio = 4.0,
    };
}

static Mel_Mixer_Source* const_source(const Mel_Alloc* a, f32 value, u32 frames, u32 samplerate)
{
    f32* buf = mel_alloc(a, sizeof(f32) * frames);
    for (u32 i = 0; i < frames; i++)
        buf[i] = value;
    Mel_Mixer_Source* s = mel_mixer_pcm_from_float(a, buf, frames, 1u, samplerate, MEL_MIXER_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return s;
}

MEL_TEST(taps, master_tap_equals_rendered_output)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = const_source(a, 0.5f, SR, SR);
    mel_mixer_pcm_set_loop(src, true, 0.0);
    mel_mixer_play(eng, src);

    Mel_Mixer_Tap* tap = mel_mixer_tap_open(eng, a, BLOCK * 4u);
    MEL_REQUIRE_NOT_NULL(tap);

    f32 rendered[BLOCK * CH];
    mel_mixer_render(eng, rendered, BLOCK);

    MEL_EXPECT_EQ(mel_mixer_tap_available(tap), BLOCK);
    f32 tapped[BLOCK * CH];
    MEL_EXPECT_EQ(mel_mixer_tap_read(tap, tapped, BLOCK), BLOCK);
    MEL_EXPECT(memcmp(rendered, tapped, sizeof rendered) == 0);
    MEL_EXPECT_FLOAT_EQ(tapped[0], 0.5f * PAN_CENTER, 1e-4f);

    mel_mixer_tap_close(tap);
    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(taps, voice_tap_is_post_fader_and_isolated)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* sa = const_source(a, 0.25f, SR, SR);
    Mel_Mixer_Source* sb = const_source(a, 0.5f, SR, SR);
    mel_mixer_pcm_set_loop(sa, true, 0.0);
    mel_mixer_pcm_set_loop(sb, true, 0.0);

    Mel_Mixer_Voice va = mel_mixer_play_ex(eng, sa, 0.5f, 0.0f, false);
    mel_mixer_play(eng, sb);

    Mel_Mixer_Tap* vtap = mel_mixer_voice_tap_open(eng, va, a, BLOCK * 4u);
    Mel_Mixer_Tap* mtap = mel_mixer_tap_open(eng, a, BLOCK * 4u);
    MEL_REQUIRE_NOT_NULL(vtap);
    MEL_REQUIRE_NOT_NULL(mtap);

    f32 rendered[BLOCK * CH];
    mel_mixer_render(eng, rendered, BLOCK);

    f32 voice_frames[BLOCK * CH];
    f32 master_frames[BLOCK * CH];
    MEL_EXPECT_EQ(mel_mixer_tap_read(vtap, voice_frames, BLOCK), BLOCK);
    MEL_EXPECT_EQ(mel_mixer_tap_read(mtap, master_frames, BLOCK), BLOCK);

    MEL_EXPECT_FLOAT_EQ(voice_frames[0], 0.25f * 0.5f * PAN_CENTER, 1e-4f);
    MEL_EXPECT_FLOAT_EQ(voice_frames[1], 0.25f * 0.5f * PAN_CENTER, 1e-4f);
    MEL_EXPECT_FLOAT_EQ(master_frames[0], (0.25f * 0.5f + 0.5f) * PAN_CENTER, 1e-4f);

    mel_mixer_tap_close(vtap);
    mel_mixer_tap_close(mtap);
    mel_mixer_destroy(eng);
    sa->source_free(sa, a);
    sb->source_free(sb, a);
}

MEL_TEST(taps, slow_reader_drops_are_counted)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = const_source(a, 0.5f, SR, SR);
    mel_mixer_pcm_set_loop(src, true, 0.0);
    mel_mixer_play(eng, src);

    Mel_Mixer_Tap* tap = mel_mixer_tap_open(eng, a, 64u);
    MEL_REQUIRE_NOT_NULL(tap);

    f32 rendered[BLOCK * CH];
    mel_mixer_render(eng, rendered, BLOCK);
    mel_mixer_render(eng, rendered, BLOCK);

    MEL_EXPECT_EQ(mel_mixer_tap_available(tap), 64u);
    MEL_EXPECT_EQ(mel_mixer_tap_dropped_frames(tap), (u64)(2u * BLOCK - 64u));

    mel_mixer_tap_close(tap);
    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(taps, voice_tap_dies_with_voice_and_drains)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Mixer_Source* src = const_source(a, 0.5f, 100u, SR);
    Mel_Mixer_Voice   v = mel_mixer_play(eng, src);

    Mel_Mixer_Tap* tap = mel_mixer_voice_tap_open(eng, v, a, BLOCK * 4u);
    MEL_REQUIRE_NOT_NULL(tap);

    f32 rendered[BLOCK * CH];
    mel_mixer_render(eng, rendered, BLOCK);
    MEL_EXPECT(!mel_mixer_voice_valid(eng, v));

    u32 drained = mel_mixer_tap_available(tap);
    MEL_EXPECT(drained > 0u);
    f32 buf[BLOCK * CH];
    MEL_EXPECT_EQ(mel_mixer_tap_read(tap, buf, BLOCK), drained);

    mel_mixer_render(eng, rendered, BLOCK);
    MEL_EXPECT_EQ(mel_mixer_tap_available(tap), 0u);
    MEL_EXPECT_EQ(mel_mixer_tap_read(tap, buf, BLOCK), 0u);

    mel_mixer_tap_close(tap);
    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

typedef struct
{
    f32 fill;
    u32 limit;
    u32 calls;
} Pull_Ctx;

static u32 pull_fill(void* user, f32* dst, u32 frames)
{
    Pull_Ctx* ctx = user;
    u32       give = frames < ctx->limit ? frames : ctx->limit;
    for (u32 i = 0; i < give; i++)
        dst[i] = ctx->fill;
    ctx->calls++;
    return give;
}

MEL_TEST(pull_source, short_reads_pad_silence_and_voice_stays_live)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Pull_Ctx          ctx = { .fill = 0.5f, .limit = 16u };
    Mel_Mixer_Source* src = mel_mixer_pull_source(a, pull_fill, &ctx, 1u, SR);
    MEL_REQUIRE_NOT_NULL(src);

    Mel_Mixer_Voice v = mel_mixer_play(eng, src);
    MEL_REQUIRE(mel_mixer_voice_valid(eng, v));

    f32 rendered[BLOCK * CH];
    mel_mixer_render(eng, rendered, BLOCK);

    MEL_EXPECT_FLOAT_EQ(rendered[0], 0.5f * PAN_CENTER, 1e-4f);
    MEL_EXPECT_FLOAT_EQ(rendered[(BLOCK - 1u) * CH], 0.0f, 1e-6f);
    MEL_EXPECT(ctx.calls > 0u);
    MEL_EXPECT(mel_mixer_voice_valid(eng, v));

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(pull_source, format_lowers_through_the_engine)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Pull_Ctx          ctx = { .fill = 0.5f, .limit = 0xFFFFFFFFu };
    Mel_Mixer_Source* src = mel_mixer_pull_source(a, pull_fill, &ctx, 1u, SR / 2u);
    MEL_REQUIRE_NOT_NULL(src);

    mel_mixer_play(eng, src);

    f32 rendered[BLOCK * CH];
    mel_mixer_render(eng, rendered, BLOCK);

    MEL_EXPECT_FLOAT_EQ(rendered[8u * CH], 0.5f * PAN_CENTER, 1e-4f);
    MEL_EXPECT_FLOAT_EQ(rendered[8u * CH + 1u], 0.5f * PAN_CENTER, 1e-4f);

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(pull_source, single_instance_refuses_a_second_voice)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Mixer*       eng = mel_mixer_create_offline(a, opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Pull_Ctx          ctx = { .fill = 0.5f, .limit = 0xFFFFFFFFu };
    Mel_Mixer_Source* src = mel_mixer_pull_source(a, pull_fill, &ctx, 1u, SR);
    MEL_REQUIRE_NOT_NULL(src);

    Mel_Mixer_Voice v1 = mel_mixer_play(eng, src);
    MEL_REQUIRE(mel_mixer_voice_valid(eng, v1));

    Mel_Mixer_Voice v2 = mel_mixer_play(eng, src);
    MEL_EXPECT(!mel_mixer_voice_valid(eng, v2));

    mel_mixer_stop(eng, v1);
    f32 rendered[BLOCK * CH];
    mel_mixer_render(eng, rendered, BLOCK);
    MEL_EXPECT(!mel_mixer_voice_valid(eng, v1));

    Mel_Mixer_Voice v3 = mel_mixer_play(eng, src);
    MEL_EXPECT(mel_mixer_voice_valid(eng, v3));

    mel_mixer_destroy(eng);
    src->source_free(src, a);
}
