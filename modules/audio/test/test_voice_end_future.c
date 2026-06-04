#include <test/test.h>

#include <audio/audio.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <allocator/tracking.h>
#include <core/types.h>
#include <future/future.h>
#include <event/event.h>
#include <executor/executor.h>

#define SR    48000u
#define CH    2u
#define BLOCK 256u

static Mel_Audio_Opt offline_opt(void)
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

static Mel_Audio_Source* const_source(const Mel_Alloc* a, u32 frames, f32 value)
{
    f32* buf = mel_alloc(a, sizeof(f32) * frames);
    for (u32 i = 0; i < frames; i++)
        buf[i] = value;
    Mel_Audio_Source* s = mel_audio_pcm_from_float(a, buf, frames, 1u, SR, MEL_AUDIO_OWNERSHIP_OWNED);
    mel_dealloc(a, buf);
    return s;
}

typedef struct
{
    Mel_Task task;
    u32      fired;
} End_Cont;

static void end_cont_run(Mel_Task* self)
{
    End_Cont* c = (End_Cont*)((char*)self - __builtin_offsetof(End_Cont, task));
    c->fired = 1u;
}

MEL_TEST(voice_end, resolves_when_source_exhausts)
{
    Mel_Track_Allocator tracker;
    mel_track_init(&tracker, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
    Mel_Alloc        tracked = mel_track_allocator(&tracker);
    const Mel_Alloc* a = &tracked;

    Mel_Audio* eng = mel_audio_create_offline(a, NULL, offline_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = const_source(a, 8u, 1.0f);
    Mel_Audio_Voice   v = mel_audio_play_ex(eng, src, 1.0f, 0.0f, false);

    Mel_Future* fut = mel_audio_voice_end_future(eng, v);
    MEL_REQUIRE_NOT_NULL(fut);

    End_Cont cont = { 0 };
    mel_task_init(&cont.task, end_cont_run);
    mel_future_then(fut, &cont.task, mel_executor_inline());

    MEL_EXPECT(!mel_future_resolved(fut));
    MEL_EXPECT_EQ(cont.fired, 0u);

    f32 out[20u * CH];
    mel_audio_render(eng, out, 20u);

    MEL_EXPECT(mel_future_resolved(fut));
    MEL_EXPECT_EQ(cont.fired, 1u);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);

    mel_audio_voice_end_future_free(eng, fut);

    mel_audio_destroy(eng);
    src->source_free(src, a);

    Mel_Track_Allocator_Stats s = mel_track_stats(&tracker);
    MEL_EXPECT_EQ(s.live_allocs, (usize)0);
    MEL_EXPECT_EQ(s.live_bytes, (usize)0);
    mel_track_shutdown(&tracker);
}

MEL_TEST(voice_end, resolves_on_explicit_stop)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, offline_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = const_source(a, SR, 0.5f);
    mel_audio_pcm_set_loop(src, true, 0.0);
    Mel_Audio_Voice v = mel_audio_play_ex(eng, src, 1.0f, 0.0f, false);

    Mel_Future* fut = mel_audio_voice_end_future(eng, v);
    MEL_REQUIRE_NOT_NULL(fut);

    f32 out[BLOCK * CH];
    mel_audio_render(eng, out, BLOCK);
    MEL_EXPECT(!mel_future_resolved(fut));

    mel_audio_stop(eng, v);
    mel_audio_render(eng, out, BLOCK);
    MEL_EXPECT(mel_future_resolved(fut));

    mel_audio_voice_end_future_free(eng, fut);

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(voice_end, resolves_when_requested_after_end)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, offline_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = const_source(a, 8u, 1.0f);
    Mel_Audio_Voice   v = mel_audio_play_ex(eng, src, 1.0f, 0.0f, false);

    f32 out[20u * CH];
    mel_audio_render(eng, out, 20u);
    MEL_EXPECT_EQ(mel_audio_active_voice_count(eng), 0u);

    Mel_Future* fut = mel_audio_voice_end_future(eng, v);
    MEL_REQUIRE_NOT_NULL(fut);
    MEL_EXPECT(mel_future_resolved(fut));

    mel_audio_voice_end_future_free(eng, fut);

    mel_audio_destroy(eng);
    src->source_free(src, a);
}

MEL_TEST(voice_end, registry_bounded_by_caller_release)
{
    Mel_Track_Allocator tracker;
    mel_track_init(&tracker, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
    Mel_Alloc        tracked = mel_track_allocator(&tracker);
    const Mel_Alloc* a = &tracked;

    Mel_Audio* eng = mel_audio_create_offline(a, NULL, offline_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Audio_Source* src = const_source(a, 8u, 1.0f);

    usize stable_live_bytes = 0u;
    f32   out[20u * CH];

    const u32 warmup = 16u;
    const u32 cycles = 512u;
    for (u32 i = 0; i < cycles; i++)
    {
        Mel_Audio_Voice v = mel_audio_play_ex(eng, src, 1.0f, 0.0f, false);
        Mel_Future*     fut = mel_audio_voice_end_future(eng, v);
        MEL_REQUIRE_NOT_NULL(fut);

        mel_audio_render(eng, out, 20u);
        MEL_EXPECT(mel_future_resolved(fut));

        mel_audio_voice_end_future_free(eng, fut);

        Mel_Track_Allocator_Stats mid = mel_track_stats(&tracker);
        if (i == warmup)
            stable_live_bytes = mid.live_bytes;
        else if (i > warmup)
            MEL_EXPECT_EQ(mid.live_bytes, stable_live_bytes);
    }

    mel_audio_destroy(eng);
    src->source_free(src, a);

    Mel_Track_Allocator_Stats s = mel_track_stats(&tracker);
    MEL_EXPECT_EQ(s.live_allocs, (usize)0);
    MEL_EXPECT_EQ(s.live_bytes, (usize)0);
    mel_track_shutdown(&tracker);
}

MEL_TEST(device_events, engine_owns_a_fireable_event)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Audio*       eng = mel_audio_create_offline(a, NULL, offline_opt());
    MEL_REQUIRE_NOT_NULL(eng);

    Mel_Event* ev = mel_audio_device_events(eng);
    MEL_REQUIRE_NOT_NULL(ev);

    mel_audio_destroy(eng);
}
