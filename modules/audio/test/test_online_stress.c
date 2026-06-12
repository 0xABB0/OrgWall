#include <test/test.h>

#include <audio/audio.h>
#include <audioout/audioout.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <allocator/tracking.h>
#include <core/types.h>
#include <thread/thread.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

static bool device_path_safe(void)
{
    return getenv("MEL_TEST_NOFORK") != NULL;
}

static bool device_plane_up(void)
{
    if (!device_path_safe())
        return false;
    mel_audioout_init(mel_alloc_heap(), NULL);
    if (mel_audioout_alive(mel_audioout_default()))
        return true;
    mel_audioout_shutdown();
    return false;
}

static void device_plane_down(void) { mel_audioout_shutdown(); }

#define SR             48000u
#define CH             2u
#define BLOCK          512u
#define MAX_VOICE_CH   6u
#define MAX_VOICE_RATIO 4.0

static Mel_Audio_Opt online_opt(void)
{
    return (Mel_Audio_Opt){
        .samplerate = SR,
        .channels = CH,
        .block_frames = BLOCK,
        .ring_blocks = 4u,
        .master_volume = 0.25f,
        .resampler = NULL,
        .exec = NULL,
        .max_voice_channels = MAX_VOICE_CH,
        .max_voice_ratio = MAX_VOICE_RATIO,
    };
}

static Mel_Audio_Opt offline_opt(void)
{
    Mel_Audio_Opt opt = online_opt();
    return opt;
}

static Mel_Audio_Source* sine_source(const Mel_Alloc* a, u32 frames, u32 channels, u32 samplerate, f32 freq)
{
    f32* buf = mel_alloc(a, sizeof(f32) * (usize)frames * (usize)channels);
    for (u32 i = 0; i < frames; i++)
    {
        f32 t = (f32)i / (f32)samplerate;
        f32 s = __builtin_sinf(6.2831853f * freq * t) * 0.5f;
        for (u32 c = 0; c < channels; c++)
            buf[(usize)i * channels + c] = s;
    }
    Mel_Audio_Source* src = mel_audio_pcm_from_float(a, buf, frames, channels, samplerate, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return src;
}

typedef struct
{
    Mel_Audio* eng;
    Mel_Audio_Source** sources;
    u32 source_count;
    _Atomic(u32) stop;
    _Atomic(u32) plays;
} Stress_Ctx;

static int stress_player(void* user)
{
    Stress_Ctx* ctx = (Stress_Ctx*)user;
    u32 i = 0;
    u32 plays = 0;
    while (atomic_load_explicit(&ctx->stop, memory_order_acquire) == 0u)
    {
        Mel_Audio_Source* src = ctx->sources[i % ctx->source_count];
        Mel_Audio_Voice   v = mel_audio_play_ex(ctx->eng, src, 0.3f, ((f32)(i % 3u) - 1.0f), false);
        plays++;

        if ((i & 7u) == 0u)
            mel_audio_set_play_speed(ctx->eng, v, 1.0 + (f64)(i % 4u) * 0.7);
        if ((i & 15u) == 0u)
            mel_audio_stop(ctx->eng, v);
        if ((i & 63u) == 0u)
            mel_audio_stop_all(ctx->eng);

        i++;
        if ((i & 31u) == 0u)
            mel_thread_sleep(200000);
    }
    atomic_fetch_add_explicit(&ctx->plays, plays, memory_order_relaxed);
    return 0;
}

static void run_concurrent_burst(const Mel_Alloc* a, Mel_Audio* eng, Mel_Audio_Source** sources, u32 source_count, i64 run_ns)
{
    Stress_Ctx ctx = {
        .eng = eng,
        .sources = sources,
        .source_count = source_count,
        .stop = 0u,
        .plays = 0u,
    };

    Mel_Thread p0;
    Mel_Thread p1;
    MEL_REQUIRE(mel_thread_spawn(&p0, stress_player, &ctx, .name = "stress-p0"));
    MEL_REQUIRE(mel_thread_spawn(&p1, stress_player, &ctx, .name = "stress-p1"));

    mel_thread_sleep(run_ns);

    atomic_store_explicit(&ctx.stop, 1u, memory_order_release);
    mel_thread_join(&p0, NULL);
    mel_thread_join(&p1, NULL);

    MEL_UNUSED(a);
}

static Mel_Audio_Source** make_sources(const Mel_Alloc* a, u32* out_count)
{
    u32                count = 4u;
    Mel_Audio_Source** s = mel_alloc(a, sizeof(Mel_Audio_Source*) * count);
    s[0] = sine_source(a, SR, 1u, SR, 220.0f);
    s[1] = sine_source(a, SR, 2u, SR, 330.0f);
    s[2] = sine_source(a, SR / 2u, MAX_VOICE_CH, SR, 440.0f);
    s[3] = sine_source(a, SR, 2u, SR / 2u, 550.0f);
    for (u32 i = 0; i < count; i++)
        mel_audio_pcm_set_loop(s[i], true, 0.0);
    *out_count = count;
    return s;
}

static void free_sources(const Mel_Alloc* a, Mel_Audio_Source** s, u32 count)
{
    for (u32 i = 0; i < count; i++)
        s[i]->source_free(s[i], a);
    mel_dealloc(a, s);
}

MEL_TEST(online, live_play_destroy_no_crash_no_leak)
{
    Mel_Track_Allocator tracker;
    mel_track_init(&tracker, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
    Mel_Alloc        tracked = mel_track_allocator(&tracker);
    const Mel_Alloc* a = &tracked;

    Mel_Audio* eng = NULL;
    bool       online = false;
    bool       plane = device_plane_up();
    if (plane)
    {
        eng = mel_audio_create(a, online_opt());
        online = eng != NULL;
        if (!online)
            device_plane_down();
    }

    if (!online)
    {
        if (!device_path_safe())
            fprintf(stderr, "      [online] fork-isolated run; CoreAudio cannot init after fork() — using offline concurrent harness (set MEL_TEST_NOFORK=1 to drive the real device)\n");
        else
            fprintf(stderr, "      [online] no CoreAudio device this run; falling back to offline concurrent harness\n");
        eng = mel_audio_create_offline(a, offline_opt());
        MEL_REQUIRE_NOT_NULL(eng);
    }
    else
    {
        fprintf(stderr, "      [online] CoreAudio device live; driving ~1.5s of real audio under concurrent play/destroy\n");
    }

    u32                source_count = 0u;
    Mel_Audio_Source** sources = make_sources(a, &source_count);

    if (!online)
    {
        Stress_Ctx ctx = {
            .eng = eng,
            .sources = sources,
            .source_count = source_count,
            .stop = 0u,
            .plays = 0u,
        };

        Mel_Thread p0;
        MEL_REQUIRE(mel_thread_spawn(&p0, stress_player, &ctx, .name = "stress-off"));

        f32* dst = mel_alloc(a, sizeof(f32) * BLOCK * CH);
        for (u32 blk = 0; blk < 6000u; blk++)
        {
            mel_audio_render(eng, dst, BLOCK);
            if ((blk & 255u) == 0u)
                mel_thread_sleep(50000);
        }
        atomic_store_explicit(&ctx.stop, 1u, memory_order_release);
        mel_thread_join(&p0, NULL);
        mel_dealloc(a, dst);
    }
    else
    {
        run_concurrent_burst(a, eng, sources, source_count, 1500000000);
    }

    mel_audio_destroy(eng);

    if (online)
    {
        Mel_Audio* eng2 = mel_audio_create(a, online_opt());
        MEL_REQUIRE_NOT_NULL(eng2);
        for (u32 i = 0; i < source_count; i++)
            mel_audio_play_ex(eng2, sources[i], 0.2f, 0.0f, false);
        mel_audio_destroy(eng2);
    }

    free_sources(a, sources, source_count);

    if (online)
        device_plane_down();

    Mel_Track_Allocator_Stats s = mel_track_stats(&tracker);
    fprintf(stderr, "      [online] live_allocs=%zu live_bytes=%zu peak_allocs=%zu\n", s.live_allocs, s.live_bytes, s.peak_allocs);
    MEL_EXPECT_EQ(s.live_allocs, (usize)0);
    MEL_EXPECT_EQ(s.live_bytes, (usize)0);

    mel_track_shutdown(&tracker);
}

MEL_TEST(online, play_then_immediate_destroy)
{
    Mel_Track_Allocator tracker;
    mel_track_init(&tracker, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
    Mel_Alloc        tracked = mel_track_allocator(&tracker);
    const Mel_Alloc* a = &tracked;

    for (u32 iter = 0; iter < 8u; iter++)
    {
        Mel_Audio* eng = NULL;
        bool       online = false;
        if (device_path_safe())
        {
            eng = mel_audio_create(a, online_opt());
            online = eng != NULL;
        }
        if (!online)
        {
            eng = mel_audio_create_offline(a, offline_opt());
            MEL_REQUIRE_NOT_NULL(eng);
        }

        Mel_Audio_Source* s0 = sine_source(a, SR, 1u, SR, 220.0f);
        Mel_Audio_Source* s1 = sine_source(a, SR / 2u, MAX_VOICE_CH, SR, 660.0f);

        f32* dst = online ? NULL : mel_alloc(a, sizeof(f32) * BLOCK * CH);
        for (u32 i = 0; i < 32u; i++)
        {
            mel_audio_play_ex(eng, s0, 0.2f, 0.0f, false);
            Mel_Audio_Voice v = mel_audio_play_ex(eng, s1, 0.2f, 0.0f, false);
            mel_audio_set_play_speed(eng, v, 3.5);
            if (!online && (i & 7u) == 0u)
                mel_audio_render(eng, dst, BLOCK);
        }
        if (dst != NULL)
            mel_dealloc(a, dst);

        mel_audio_destroy(eng);

        s0->source_free(s0, a);
        s1->source_free(s1, a);
    }

    Mel_Track_Allocator_Stats s = mel_track_stats(&tracker);
    fprintf(stderr, "      [online] immediate-destroy live_allocs=%zu live_bytes=%zu\n", s.live_allocs, s.live_bytes);
    MEL_EXPECT_EQ(s.live_allocs, (usize)0);
    MEL_EXPECT_EQ(s.live_bytes, (usize)0);

    mel_track_shutdown(&tracker);
}
