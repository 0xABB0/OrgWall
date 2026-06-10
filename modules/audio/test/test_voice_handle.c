#include <test/test.h>

#include <audio/audio.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.fwd.h>
#include <core/types.h>

#define SR    48000u
#define CH    2u
#define BLOCK 256u

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

static Mel_Audio_Source* mono_const(const Mel_Alloc* a, u32 frames, f32 value)
{
    f32* buf = mel_alloc(a, sizeof(f32) * frames);
    for (u32 i = 0; i < frames; i++)
        buf[i] = value;
    Mel_Audio_Source* s = mel_audio_pcm_from_float(a, buf, frames, 1u, SR, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return s;
}

MEL_TEST(voice, play_returns_valid_handle_synchronously)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = mono_const(a, 64u, 1.0f);
    Mel_Audio_Voice   v = mel_audio_play(eng, src);

    MEL_EXPECT(mel_slotmap_handle_valid(v.slot));
    MEL_EXPECT(mel_audio_voice_valid(eng, v));

    f32 out[8u * CH];
    mel_audio_render(eng, out, 8u);
    MEL_EXPECT(mel_audio_voice_valid(eng, v));

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(voice, destroy_with_reserved_unactivated_voice)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = mono_const(a, 64u, 1.0f);
    Mel_Audio_Voice   v = mel_audio_play(eng, src);
    MEL_EXPECT(mel_audio_voice_valid(eng, v));

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(voice, reserve_bumps_count_before_activation)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);

    Mel_Audio_Source* src = mono_const(a, 64u, 1.0f);
    mel_audio_play(eng, src);

    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 1u);

    f32 out[8u * CH];
    mel_audio_render(eng, out, 8u);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 1u);

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(voice, stop_noops_via_generation)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = mono_const(a, 64u, 1.0f);
    Mel_Audio_Voice   v = mel_audio_play(eng, src);

    f32 out[8u * CH];
    mel_audio_render(eng, out, 8u);

    mel_audio_stop(eng, v);
    mel_audio_render(eng, out, 8u);

    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);
    MEL_EXPECT(!mel_audio_voice_valid(eng, v));

    mel_audio_set_volume(eng, v, 0.5f);
    mel_audio_seek(eng, v, 1.0);
    mel_audio_set_pan(eng, v, 1.0f);
    mel_audio_render(eng, out, 8u);

    for (u32 i = 0; i < 8u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 0u], 0.0f, 1e-6f);
        MEL_EXPECT_FLOAT_EQ(out[i * CH + 1u], 0.0f, 1e-6f);
    }
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(voice, slot_reuse_rolls_generation)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = mono_const(a, 64u, 1.0f);

    Mel_Audio_Voice first = mel_audio_play(eng, src);
    f32             out[8u * CH];
    mel_audio_render(eng, out, 8u);

    mel_audio_stop(eng, first);
    mel_audio_render(eng, out, 8u);
    MEL_REQUIRE_EQ(mel_audio_active_voice_count(eng), 0u);

    Mel_Audio_Voice second = mel_audio_play(eng, src);
    mel_audio_render(eng, out, 8u);

    MEL_EXPECT_EQ(first.slot.index, second.slot.index);
    MEL_EXPECT_NEQ(first.slot.generation, second.slot.generation);

    MEL_EXPECT(!mel_audio_voice_valid(eng, first));
    MEL_EXPECT(mel_audio_voice_valid(eng, second));

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(voice, count_tracks_reserve_activate_end)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);

    Mel_Audio_Source* src = mono_const(a, 16u, 1.0f);
    mel_audio_play(eng, src);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 1u);

    Mel_Audio_Source* src2 = mono_const(a, 16u, 1.0f);
    mel_audio_play(eng, src2);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 2u);

    f32 out[8u * CH];
    mel_audio_render(eng, out, 8u);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 2u);

    mel_audio_render(eng, out, 8u);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);

    mel_audio_destroy(eng);
    src->source_free(src, a);
    src2->source_free(src2, a);
}

MEL_TEST(voice, stop_all_clears_count)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Audio*       eng = mel_audio_create_offline(a, base_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* s0 = mono_const(a, 256u, 1.0f);
    Mel_Audio_Source* s1 = mono_const(a, 256u, 1.0f);
    Mel_Audio_Source* s2 = mono_const(a, 256u, 1.0f);
    mel_audio_play(eng, s0);
    mel_audio_play(eng, s1);
    mel_audio_play(eng, s2);

    f32 out[8u * CH];
    mel_audio_render(eng, out, 8u);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 3u);

    mel_audio_stop_all(eng);
    mel_audio_render(eng, out, 8u);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);

    mel_audio_destroy(eng);
    s0->source_free(s0, a);
    s1->source_free(s1, a);
    s2->source_free(s2, a);
}
