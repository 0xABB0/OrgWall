#include <test/test.h>

#include <audio/audio.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <allocator/tracking.h>
#include <core/types.h>
#include <time/nano.h>

#include <stdio.h>

#define BENCH_SR        48000u
#define BENCH_CH        2u
#define BENCH_BLOCK     256u
#define BENCH_SECONDS   5u
#define BENCH_SRC_FRAMES 4096u

typedef struct
{
    u64 alloc_count_before;
    u64 alloc_count_after;
    u64 ns_total;
    u32 blocks;
    u32 frames;
} Bench_Result;

static u64 bench_alloc_ops(Mel_Track_Allocator* t)
{
    Mel_Track_Allocator_Stats s = mel_track_stats(t);
    return (u64)s.total_alloc_count + (u64)s.total_realloc_count;
}

static Mel_Audio_Opt bench_opt(void)
{
    return (Mel_Audio_Opt){
        .samplerate = BENCH_SR,
        .channels = BENCH_CH,
        .block_frames = BENCH_BLOCK,
        .ring_blocks = 0u,
        .master_volume = 1.0f,
        .resampler = NULL,
        .exec = NULL,
    };
}

static Mel_Audio_Source* bench_stereo_source(const Mel_Alloc* a)
{
    f32* buf = mel_alloc(a, sizeof(f32) * BENCH_SRC_FRAMES * BENCH_CH);
    for (u32 i = 0; i < BENCH_SRC_FRAMES; i++)
    {
        f32 phase = (f32)i / (f32)BENCH_SRC_FRAMES;
        buf[i * BENCH_CH + 0u] = phase * 2.0f - 1.0f;
        buf[i * BENCH_CH + 1u] = 1.0f - phase * 2.0f;
    }
    Mel_Audio_Source* src = mel_audio_pcm_from_float(a, buf, BENCH_SRC_FRAMES, BENCH_CH, BENCH_SR, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    mel_audio_pcm_set_loop(src, true, 0.0);
    return src;
}

static void bench_run(u32 voices, Bench_Result* out)
{
    Mel_Track_Allocator tracker;
    mel_track_init(&tracker, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
    Mel_Alloc        tracked = mel_track_allocator(&tracker);
    const Mel_Alloc* a = &tracked;

    Mel_Audio* eng = mel_audio_create_offline(a, bench_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = bench_stereo_source(a);
    MEL_REQUIRE_NOT_NULL(src);

    for (u32 i = 0; i < voices; i++)
        mel_audio_play_ex(eng, src, 1.0f, 0.0f, false);

    f32* dst = mel_alloc(a, sizeof(f32) * BENCH_BLOCK * BENCH_CH);
    MEL_REQUIRE_NOT_NULL(dst);

    mel_audio_render(eng, dst, BENCH_BLOCK);
    MEL_REQUIRE_EQ(mel_audio_active_voice_count(eng), voices);

    u32 total_blocks = (BENCH_SR * BENCH_SECONDS) / BENCH_BLOCK;

    u64 before = bench_alloc_ops(&tracker);
    u64 t0 = mel_nanos_since_unspecified_epoch();

    for (u32 blk = 0; blk < total_blocks; blk++)
    {
        u32 produced = mel_audio_render(eng, dst, BENCH_BLOCK);
        MEL_REQUIRE_EQ(produced, BENCH_BLOCK);
    }

    u64 t1 = mel_nanos_since_unspecified_epoch();
    u64 after = bench_alloc_ops(&tracker);

    out->alloc_count_before = before;
    out->alloc_count_after = after;
    out->ns_total = t1 - t0;
    out->blocks = total_blocks;
    out->frames = total_blocks * BENCH_BLOCK;

    mel_dealloc(a, dst);
    mel_audio_destroy(eng);
    src->source_free(src, a);

    mel_track_shutdown(&tracker);
}

static void bench_report(u32 voices, const Bench_Result* r)
{
    u64 frames = (u64)r->frames;
    f64 secs = (f64)r->ns_total / 1.0e9;
    f64 fps = secs > 0.0 ? (f64)frames / secs : 0.0;
    f64 ns_per_voice_block = (voices > 0u && r->blocks > 0u)
                                 ? (f64)r->ns_total / ((f64)r->blocks * (f64)voices)
                                 : 0.0;
    u64 delta = r->alloc_count_after - r->alloc_count_before;

    fprintf(stderr,
            "      [bench] voices=%-5u frames=%llu time=%.3fms throughput=%.2f Mframes/s "
            "ns/voice/block=%.1f alloc_delta=%llu alloc_on_mix_path=%s\n",
            voices,
            (unsigned long long)frames,
            secs * 1000.0,
            fps / 1.0e6,
            ns_per_voice_block,
            (unsigned long long)delta,
            delta == 0u ? "false" : "true");
}

MEL_TEST(bench, mix_offline_1_voice)
{
    Bench_Result r;
    bench_run(1u, &r);
    bench_report(1u, &r);
    MEL_EXPECT_EQ(r.alloc_count_after, r.alloc_count_before);
}

MEL_TEST(bench, mix_offline_64_voices)
{
    Bench_Result r;
    bench_run(64u, &r);
    bench_report(64u, &r);
    MEL_EXPECT_EQ(r.alloc_count_after, r.alloc_count_before);
}

MEL_TEST(bench, mix_offline_256_voices)
{
    Bench_Result r;
    bench_run(256u, &r);
    bench_report(256u, &r);
    MEL_EXPECT_EQ(r.alloc_count_after, r.alloc_count_before);
}

MEL_TEST(bench, mix_offline_1024_voices)
{
    Bench_Result r;
    bench_run(1024u, &r);
    bench_report(1024u, &r);
    MEL_EXPECT_EQ(r.alloc_count_after, r.alloc_count_before);
}
