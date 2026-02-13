#include "anim.timeline.h"

#include <stdio.h>
#include <math.h>

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  %-50s", #name); name(); printf("PASS\n"); g_pass++; } while(0)
#define ASSERT(cond) do { if (!(cond)) { printf("FAIL (%s:%d: %s)\n", __FILE__, __LINE__, #cond); g_fail++; return; } } while(0)
#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

static Mel_Anim_Keyframe g_frames_3[] = {
    { .frame_index = 0, .duration = 0.1f },
    { .frame_index = 1, .duration = 0.1f },
    { .frame_index = 2, .duration = 0.1f },
};

static Mel_Anim_Event g_events_3[] = {
    { .flags = 0 },
    { .flags = MEL_ANIM_EVENT_TAG, .tag_hash = 0xDEAD },
    { .flags = 0 },
};

TEST(test_advance_through_keyframes)
{
    Mel_Anim_Timeline tl = {
        .keyframes = g_frames_3,
        .events = nullptr,
        .keyframe_count = 3,

        .loop = false,
    };

    Mel_Anim_Playback pb;
    mel_anim_playback_start(&pb, &tl);

    ASSERT(mel_anim_playback_frame_index(&pb) == 0);

    mel_anim_playback_update(&pb, 0.05f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 0);
    ASSERT(pb.playing);

    mel_anim_playback_update(&pb, 0.06f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 1);

    mel_anim_playback_update(&pb, 0.1f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 2);
}

TEST(test_looping_wraps)
{
    Mel_Anim_Timeline tl = {
        .keyframes = g_frames_3,
        .events = nullptr,
        .keyframe_count = 3,

        .loop = true,
    };

    Mel_Anim_Playback pb;
    mel_anim_playback_start(&pb, &tl);

    mel_anim_playback_update(&pb, 0.35f);
    ASSERT(pb.playing);
    ASSERT(!pb.finished);
    ASSERT(mel_anim_playback_frame_index(&pb) == 0);
}

TEST(test_non_loop_finishes)
{
    Mel_Anim_Timeline tl = {
        .keyframes = g_frames_3,
        .events = nullptr,
        .keyframe_count = 3,

        .loop = false,
    };

    Mel_Anim_Playback pb;
    mel_anim_playback_start(&pb, &tl);

    mel_anim_playback_update(&pb, 0.5f);
    ASSERT(!pb.playing);
    ASSERT(pb.finished);
    ASSERT(mel_anim_playback_frame_index(&pb) == 2);
}

TEST(test_events_at_correct_frame)
{
    Mel_Anim_Timeline tl = {
        .keyframes = g_frames_3,
        .events = g_events_3,
        .keyframe_count = 3,

        .loop = false,
    };

    Mel_Anim_Playback pb;
    mel_anim_playback_start(&pb, &tl);

    ASSERT(mel_anim_playback_event(&pb) == nullptr);

    mel_anim_playback_update(&pb, 0.11f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 1);

    Mel_Anim_Event* ev = mel_anim_playback_event(&pb);
    ASSERT(ev != nullptr);
    ASSERT(ev->flags & MEL_ANIM_EVENT_TAG);
    ASSERT(ev->tag_hash == 0xDEAD);

    mel_anim_playback_update(&pb, 0.1f);
    ASSERT(mel_anim_playback_event(&pb) == nullptr);
}

TEST(test_stop_restart)
{
    Mel_Anim_Timeline tl = {
        .keyframes = g_frames_3,
        .events = nullptr,
        .keyframe_count = 3,

        .loop = true,
    };

    Mel_Anim_Playback pb;
    mel_anim_playback_start(&pb, &tl);

    mel_anim_playback_update(&pb, 0.15f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 1);

    mel_anim_playback_stop(&pb);
    ASSERT(!pb.playing);

    mel_anim_playback_update(&pb, 0.5f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 1);

    mel_anim_playback_start(&pb, &tl);
    ASSERT(mel_anim_playback_frame_index(&pb) == 0);
    ASSERT(pb.playing);
}

TEST(test_duration_accumulation)
{
    Mel_Anim_Keyframe frames[] = {
        { .frame_index = 0, .duration = 0.25f },
        { .frame_index = 1, .duration = 0.5f },
        { .frame_index = 2, .duration = 0.25f },
    };

    Mel_Anim_Timeline tl = {
        .keyframes = frames,
        .events = nullptr,
        .keyframe_count = 3,

        .loop = false,
    };

    Mel_Anim_Playback pb;
    mel_anim_playback_start(&pb, &tl);

    mel_anim_playback_update(&pb, 0.26f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 1);

    mel_anim_playback_update(&pb, 0.4f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 1);

    mel_anim_playback_update(&pb, 0.11f);
    ASSERT(mel_anim_playback_frame_index(&pb) == 2);
}

int main(void)
{
    printf("anim.timeline tests:\n");

    RUN(test_advance_through_keyframes);
    RUN(test_looping_wraps);
    RUN(test_non_loop_finishes);
    RUN(test_events_at_correct_frame);
    RUN(test_stop_restart);
    RUN(test_duration_accumulation);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
