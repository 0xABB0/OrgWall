#include "anim.mixer.h"
#include "anim.clip.h"
#include "anim.track.h"
#include "math.curve.h"
#include "math.scalar.h"
#include "event.channel.h"
#include "allocator.heap.h"
#include "collection.array.h"
#include "test.harness.h"

static Mel_Anim_Clip make_f32_clip(const Mel_Alloc* alloc, u64 prop_id,
                                    f32 v0, f32 v1, f32 duration, bool loop)
{
    Mel_Anim_Clip clip;
    mel_anim_clip_init(&clip, alloc, 0, 1, 0, duration, loop);

    Mel_Anim_Track* track = mel_anim_clip_track(&clip, 0);
    mel_anim_track_init(track, alloc, prop_id, MEL_ANIM_TRACK_F32, 2);
    mel_anim_track_set_keyframe(track, 0, 0.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(track, 1, duration, &v1, MEL_CURVE_LINEAR);

    return clip;
}

MEL_TEST(single_layer_output, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, false);
    mel_anim_mixer_play(&mixer, 0, &clip, 0.0f);
    mel_anim_mixer_update(&mixer, 0.5f);

    MEL_ASSERT_EQ(mel_anim_mixer_output_count(&mixer), 1u);
    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 5.0f, 0.1f);

    mel_anim_clip_destroy(&clip, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(two_replace_layers, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);

    u32 l0 = mel_anim_mixer_add_layer(&mixer);
    u32 l1 = mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 0.0f, 1.0f, true);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 100.0f, 100.0f, 1.0f, true);

    mel_anim_mixer_play(&mixer, l0, &clip_a, 0.0f);
    mel_anim_mixer_play(&mixer, l1, &clip_b, 0.0f);

    mel_anim_mixer_layer(&mixer, l1)->weight = 0.5f;

    mel_anim_mixer_update(&mixer, 0.0f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 50.0f, 1.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(crossfade, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 0.0f, 2.0f, true);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 100.0f, 100.0f, 2.0f, true);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_update(&mixer, 0.1f);

    mel_anim_mixer_play(&mixer, 0, &clip_b, 1.0f);
    mel_anim_mixer_update(&mixer, 0.5f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT(out->value[0] > 20.0f && out->value[0] < 80.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(crossfade_completes, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 0.0f, 2.0f, true);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 100.0f, 100.0f, 2.0f, true);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_play(&mixer, 0, &clip_b, 0.5f);
    mel_anim_mixer_update(&mixer, 1.0f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT_NULL(l->mix_from);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 100.0f, 1.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(looping_wraps, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, true);
    mel_anim_mixer_play(&mixer, 0, &clip, 0.0f);
    mel_anim_mixer_update(&mixer, 1.5f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT(l->playing);
    MEL_ASSERT(!l->finished);
    MEL_ASSERT(l->time < 1.0f);

    mel_anim_clip_destroy(&clip, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(non_loop_finishes, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, false);
    mel_anim_mixer_play(&mixer, 0, &clip, 0.0f);
    mel_anim_mixer_update(&mixer, 2.0f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT(l->finished);

    mel_anim_clip_destroy(&clip, alloc);
    mel_anim_mixer_destroy(&mixer);
}

static u32 s_event_count = 0;
static u64 s_event_ids[8] = {0};

static bool mel__is_lifecycle_event(u64 id)
{
    return id >= 0xFFFFFFFFFFFF0001ULL && id <= 0xFFFFFFFFFFFF0004ULL;
}

static void mixer_event_cb(void* ctx, const void* event)
{
    MEL_UNUSED(ctx);
    const Mel_Anim_Fired_Event* e = event;
    if (mel__is_lifecycle_event(e->event_id))
        return;
    if (s_event_count < 8)
        s_event_ids[s_event_count] = e->event_id;
    s_event_count++;
}

MEL_TEST(events_collected, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Event_Sub sub = mel_event_channel_on(&mixer.events, mixer_event_cb, NULL);

    Mel_Anim_Clip clip;
    mel_anim_clip_init(&clip, alloc, 0, 1, 2, 2.0f, false);

    Mel_Anim_Track* track = mel_anim_clip_track(&clip, 0);
    mel_anim_track_init(track, alloc, 1, MEL_ANIM_TRACK_F32, 2);
    f32 v0 = 0.0f, v1 = 1.0f;
    mel_anim_track_set_keyframe(track, 0, 0.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(track, 1, 2.0f, &v1, MEL_CURVE_LINEAR);

    clip.events[0] = (Mel_Anim_Clip_Event){ .time = 0.5f, .event_id = 10 };
    clip.events[1] = (Mel_Anim_Clip_Event){ .time = 1.5f, .event_id = 20 };

    mel_anim_mixer_play(&mixer, 0, &clip, 0.0f);
    mel_anim_mixer_update(&mixer, 1.0f);

    s_event_count = 0;
    mel_anim_mixer_flush_events(&mixer);
    MEL_ASSERT_EQ(s_event_count, 1u);
    MEL_ASSERT_EQ(s_event_ids[0], 10ull);

    mel_anim_mixer_update(&mixer, 1.0f);

    s_event_count = 0;
    mel_anim_mixer_flush_events(&mixer);
    MEL_ASSERT_EQ(s_event_count, 1u);
    MEL_ASSERT_EQ(s_event_ids[0], 20ull);

    mel_event_channel_off(&mixer.events, sub);
    mel_anim_clip_destroy(&clip, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(add_blend_mode, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);

    u32 l0 = mel_anim_mixer_add_layer(&mixer);
    u32 l1 = mel_anim_mixer_add_layer(&mixer);
    mel_anim_mixer_layer(&mixer, l1)->mix_mode = MEL_ANIM_MIX_ADD;

    Mel_Anim_Clip base = make_f32_clip(alloc, 1, 50.0f, 50.0f, 1.0f, true);
    Mel_Anim_Clip additive = make_f32_clip(alloc, 1, 10.0f, 10.0f, 1.0f, true);

    mel_anim_mixer_play(&mixer, l0, &base, 0.0f);
    mel_anim_mixer_play(&mixer, l1, &additive, 0.0f);

    mel_anim_mixer_update(&mixer, 0.0f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 60.0f, 1.0f);

    mel_anim_clip_destroy(&base, alloc);
    mel_anim_clip_destroy(&additive, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(default_values_setup_pose, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    f32 default_rot = 45.0f;
    mel_anim_mixer_set_default(&mixer, 100, 1, &default_rot, NULL);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 100, 90.0f, 90.0f, 1.0f, false);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 200, 0.0f, 10.0f, 1.0f, false);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_update(&mixer, 0.5f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 100);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 90.0f, 1.0f);

    mel_anim_mixer_play(&mixer, 0, &clip_b, 1.0f);
    mel_anim_mixer_update(&mixer, 0.5f);

    out = mel_anim_mixer_find_output(&mixer, 100);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT(out->value[0] > 45.0f && out->value[0] < 90.0f);

    mel_anim_mixer_update(&mixer, 10.0f);
    out = mel_anim_mixer_find_output(&mixer, 100);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 45.0f, 1.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

static u32 s_lifecycle_count = 0;
static u64 s_lifecycle_ids[16] = {0};

static void lifecycle_event_cb(void* ctx, const void* event)
{
    MEL_UNUSED(ctx);
    const Mel_Anim_Fired_Event* e = event;
    if (!mel__is_lifecycle_event(e->event_id))
        return;
    if (s_lifecycle_count < 16)
        s_lifecycle_ids[s_lifecycle_count] = e->event_id;
    s_lifecycle_count++;
}

MEL_TEST(lifecycle_started, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Event_Sub sub = mel_event_channel_on(&mixer.events, lifecycle_event_cb, NULL);

    Mel_Anim_Clip clip = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, false);

    s_lifecycle_count = 0;
    mel_anim_mixer_play(&mixer, 0, &clip, 0.0f);
    mel_anim_mixer_flush_events(&mixer);

    MEL_ASSERT_EQ(s_lifecycle_count, 1u);
    MEL_ASSERT_EQ(s_lifecycle_ids[0], MEL_ANIM_EVENT_STARTED);

    mel_event_channel_off(&mixer.events, sub);
    mel_anim_clip_destroy(&clip, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(lifecycle_completed, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Event_Sub sub = mel_event_channel_on(&mixer.events, lifecycle_event_cb, NULL);

    Mel_Anim_Clip clip = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, false);
    mel_anim_mixer_play(&mixer, 0, &clip, 0.0f);
    mel_anim_mixer_flush_events(&mixer);

    s_lifecycle_count = 0;
    mel_anim_mixer_update(&mixer, 2.0f);
    mel_anim_mixer_flush_events(&mixer);

    MEL_ASSERT_GE(s_lifecycle_count, 1u);
    bool found_completed = false;
    for (u32 i = 0; i < s_lifecycle_count; i++)
    {
        if (s_lifecycle_ids[i] == MEL_ANIM_EVENT_COMPLETED)
            found_completed = true;
    }
    MEL_ASSERT(found_completed);

    mel_event_channel_off(&mixer.events, sub);
    mel_anim_clip_destroy(&clip, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(lifecycle_looped, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Event_Sub sub = mel_event_channel_on(&mixer.events, lifecycle_event_cb, NULL);

    Mel_Anim_Clip clip = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, true);
    mel_anim_mixer_play(&mixer, 0, &clip, 0.0f);
    mel_anim_mixer_flush_events(&mixer);

    s_lifecycle_count = 0;
    mel_anim_mixer_update(&mixer, 1.5f);
    mel_anim_mixer_flush_events(&mixer);

    bool found_looped = false;
    for (u32 i = 0; i < s_lifecycle_count; i++)
    {
        if (s_lifecycle_ids[i] == MEL_ANIM_EVENT_LOOPED)
            found_looped = true;
    }
    MEL_ASSERT(found_looped);

    mel_event_channel_off(&mixer.events, sub);
    mel_anim_clip_destroy(&clip, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(lifecycle_interrupted, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Event_Sub sub = mel_event_channel_on(&mixer.events, lifecycle_event_cb, NULL);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 10.0f, 2.0f, false);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 0.0f, 10.0f, 2.0f, false);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_update(&mixer, 0.5f);
    mel_anim_mixer_flush_events(&mixer);

    s_lifecycle_count = 0;
    mel_anim_mixer_play(&mixer, 0, &clip_b, 0.0f);
    mel_anim_mixer_flush_events(&mixer);

    MEL_ASSERT_GE(s_lifecycle_count, 1u);
    MEL_ASSERT_EQ(s_lifecycle_ids[0], MEL_ANIM_EVENT_INTERRUPTED);

    mel_event_channel_off(&mixer.events, sub);
    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(queue_plays_after_finish, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, false);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 100.0f, 100.0f, 1.0f, false);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_queue(&mixer, 0, &clip_b, 0.0f);

    mel_anim_mixer_update(&mixer, 2.0f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT(l->clip == &clip_b);
    MEL_ASSERT(!l->finished);

    mel_anim_mixer_update(&mixer, 0.5f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 100.0f, 1.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(queue_cleared_on_manual_play, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, false);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 50.0f, 50.0f, 1.0f, false);
    Mel_Anim_Clip clip_c = make_f32_clip(alloc, 1, 200.0f, 200.0f, 1.0f, false);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_queue(&mixer, 0, &clip_b, 0.0f);

    mel_anim_mixer_play(&mixer, 0, &clip_c, 0.0f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT_EQ(l->queue.count, (usize)0);
    MEL_ASSERT(l->clip == &clip_c);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_clip_destroy(&clip_c, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(queue_not_triggered_on_loop, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_loop = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, true);
    Mel_Anim_Clip clip_queued = make_f32_clip(alloc, 1, 100.0f, 100.0f, 1.0f, false);

    mel_anim_mixer_play(&mixer, 0, &clip_loop, 0.0f);
    mel_anim_mixer_queue(&mixer, 0, &clip_queued, 0.0f);

    mel_anim_mixer_update(&mixer, 3.0f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT(l->clip == &clip_loop);
    MEL_ASSERT_EQ(l->queue.count, (usize)1);

    mel_anim_clip_destroy(&clip_loop, alloc);
    mel_anim_clip_destroy(&clip_queued, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(nested_crossfade_chain_exists, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 0.0f, 2.0f, true);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 50.0f, 50.0f, 2.0f, true);
    Mel_Anim_Clip clip_c = make_f32_clip(alloc, 1, 100.0f, 100.0f, 2.0f, true);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_update(&mixer, 0.1f);

    mel_anim_mixer_play(&mixer, 0, &clip_b, 1.0f);
    mel_anim_mixer_update(&mixer, 0.1f);

    mel_anim_mixer_play(&mixer, 0, &clip_c, 1.0f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT_NOT_NULL(l->mix_from);
    MEL_ASSERT(l->mix_from->clip == &clip_b);
    MEL_ASSERT_NOT_NULL(l->mix_from->from);
    MEL_ASSERT(l->mix_from->from->clip == &clip_a);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_clip_destroy(&clip_c, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(nested_crossfade_chain_pruned, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 0.0f, 2.0f, true);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 50.0f, 50.0f, 2.0f, true);
    Mel_Anim_Clip clip_c = make_f32_clip(alloc, 1, 100.0f, 100.0f, 2.0f, true);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_update(&mixer, 0.1f);

    mel_anim_mixer_play(&mixer, 0, &clip_b, 0.5f);
    mel_anim_mixer_update(&mixer, 0.1f);

    mel_anim_mixer_play(&mixer, 0, &clip_c, 1.0f);
    mel_anim_mixer_update(&mixer, 2.0f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT_NULL(l->mix_from);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 100.0f, 1.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_clip_destroy(&clip_c, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(queue_lifecycle_sequence, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Event_Sub sub = mel_event_channel_on(&mixer.events, lifecycle_event_cb, NULL);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, false);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 100.0f, 100.0f, 1.0f, false);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_queue(&mixer, 0, &clip_b, 0.0f);
    mel_anim_mixer_flush_events(&mixer);

    s_lifecycle_count = 0;
    mel_anim_mixer_update(&mixer, 2.0f);
    mel_anim_mixer_flush_events(&mixer);

    bool found_completed = false;
    bool found_started = false;
    bool found_interrupted = false;
    for (u32 i = 0; i < s_lifecycle_count; i++)
    {
        if (s_lifecycle_ids[i] == MEL_ANIM_EVENT_COMPLETED) found_completed = true;
        if (s_lifecycle_ids[i] == MEL_ANIM_EVENT_STARTED)   found_started = true;
        if (s_lifecycle_ids[i] == MEL_ANIM_EVENT_INTERRUPTED) found_interrupted = true;
    }
    MEL_ASSERT(found_completed);
    MEL_ASSERT(found_started);
    MEL_ASSERT(!found_interrupted);

    mel_event_channel_off(&mixer.events, sub);
    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(stop_fires_interrupted, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Event_Sub sub = mel_event_channel_on(&mixer.events, lifecycle_event_cb, NULL);

    Mel_Anim_Clip clip = make_f32_clip(alloc, 1, 0.0f, 10.0f, 2.0f, false);
    mel_anim_mixer_play(&mixer, 0, &clip, 0.0f);
    mel_anim_mixer_update(&mixer, 0.5f);
    mel_anim_mixer_flush_events(&mixer);

    s_lifecycle_count = 0;
    mel_anim_mixer_stop(&mixer, 0);
    mel_anim_mixer_flush_events(&mixer);

    MEL_ASSERT_GE(s_lifecycle_count, 1u);
    MEL_ASSERT_EQ(s_lifecycle_ids[0], MEL_ANIM_EVENT_INTERRUPTED);

    mel_event_channel_off(&mixer.events, sub);
    mel_anim_clip_destroy(&clip, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(default_values_with_custom_blend, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    f32 default_angle = 350.0f * (MEL_PI / 180.0f);
    mel_anim_mixer_set_default(&mixer, 100, 1, &default_angle, mel_blend_angle);

    f32 target_angle = 10.0f * (MEL_PI / 180.0f);
    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 100, target_angle, target_angle, 1.0f, true);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 200, 0.0f, 10.0f, 2.0f, true);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_update(&mixer, 0.1f);

    mel_anim_mixer_play(&mixer, 0, &clip_b, 1.0f);
    mel_anim_mixer_update(&mixer, 0.5f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 100);
    MEL_ASSERT_NOT_NULL(out);

    f32 angle_from_zero = mel_absf(mel_angle_diff(out->value[0], 0.0f));
    MEL_ASSERT(angle_from_zero < 0.5f);

    f32 angle_from_180 = mel_absf(mel_angle_diff(out->value[0], MEL_PI));
    MEL_ASSERT(angle_from_180 > 1.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(queued_transition_with_crossfade, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 10.0f, 1.0f, false);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 100.0f, 100.0f, 2.0f, false);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_queue(&mixer, 0, &clip_b, 2.0f);

    mel_anim_mixer_update(&mixer, 1.01f);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT(l->clip == &clip_b);
    MEL_ASSERT_NOT_NULL(l->mix_from);
    MEL_ASSERT(l->mix_from->clip == &clip_a);

    mel_anim_mixer_update(&mixer, 5.0f);

    l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT_NULL(l->mix_from);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 100.0f, 1.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(add_blend_partial_weight, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);

    u32 l0 = mel_anim_mixer_add_layer(&mixer);
    u32 l1 = mel_anim_mixer_add_layer(&mixer);
    mel_anim_mixer_layer(&mixer, l1)->mix_mode = MEL_ANIM_MIX_ADD;
    mel_anim_mixer_layer(&mixer, l1)->weight = 0.5f;

    Mel_Anim_Clip base = make_f32_clip(alloc, 1, 50.0f, 50.0f, 1.0f, true);
    Mel_Anim_Clip additive = make_f32_clip(alloc, 1, 20.0f, 20.0f, 1.0f, true);

    mel_anim_mixer_play(&mixer, l0, &base, 0.0f);
    mel_anim_mixer_play(&mixer, l1, &additive, 0.0f);
    mel_anim_mixer_update(&mixer, 0.0f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 60.0f, 1.0f);

    mel_anim_clip_destroy(&base, alloc);
    mel_anim_clip_destroy(&additive, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(add_blend_during_crossfade, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);

    u32 l0 = mel_anim_mixer_add_layer(&mixer);
    u32 l1 = mel_anim_mixer_add_layer(&mixer);
    mel_anim_mixer_layer(&mixer, l1)->mix_mode = MEL_ANIM_MIX_ADD;

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 0.0f, 2.0f, true);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 100.0f, 100.0f, 2.0f, true);
    Mel_Anim_Clip clip_add = make_f32_clip(alloc, 1, 30.0f, 30.0f, 2.0f, true);

    mel_anim_mixer_play(&mixer, l0, &clip_a, 0.0f);
    mel_anim_mixer_update(&mixer, 0.1f);

    mel_anim_mixer_play(&mixer, l0, &clip_b, 1.0f);
    mel_anim_mixer_play(&mixer, l1, &clip_add, 0.0f);
    mel_anim_mixer_update(&mixer, 0.5f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);

    MEL_ASSERT(out->value[0] > 60.0f && out->value[0] < 90.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_clip_destroy(&clip_add, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(nested_crossfade_intermediate_values, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 0.0f, 0.0f, 4.0f, true);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 100.0f, 100.0f, 4.0f, true);
    Mel_Anim_Clip clip_c = make_f32_clip(alloc, 1, 200.0f, 200.0f, 4.0f, true);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_update(&mixer, 0.1f);

    mel_anim_mixer_play(&mixer, 0, &clip_b, 2.0f);
    mel_anim_mixer_update(&mixer, 0.5f);

    mel_anim_mixer_play(&mixer, 0, &clip_c, 2.0f);
    mel_anim_mixer_update(&mixer, 0.5f);

    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);

    MEL_ASSERT(out->value[0] > 30.0f && out->value[0] < 120.0f);

    MEL_ASSERT(out->value[0] > 0.0f);
    MEL_ASSERT(out->value[0] < 200.0f);

    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_clip_destroy(&clip_c, alloc);
    mel_anim_mixer_destroy(&mixer);
}

MEL_TEST(multiple_queued_entries, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Event_Sub sub = mel_event_channel_on(&mixer.events, lifecycle_event_cb, NULL);

    Mel_Anim_Clip clip_a = make_f32_clip(alloc, 1, 10.0f, 10.0f, 0.5f, false);
    Mel_Anim_Clip clip_b = make_f32_clip(alloc, 1, 50.0f, 50.0f, 0.5f, false);
    Mel_Anim_Clip clip_c = make_f32_clip(alloc, 1, 90.0f, 90.0f, 0.5f, false);

    mel_anim_mixer_play(&mixer, 0, &clip_a, 0.0f);
    mel_anim_mixer_queue(&mixer, 0, &clip_b, 0.0f);
    mel_anim_mixer_queue(&mixer, 0, &clip_c, 0.0f);

    mel_anim_mixer_update(&mixer, 0.6f);
    mel_anim_mixer_flush_events(&mixer);

    Mel_Anim_Layer* l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT(l->clip == &clip_b);
    MEL_ASSERT_EQ(l->queue.count, (usize)1);

    mel_anim_mixer_update(&mixer, 0.6f);
    mel_anim_mixer_flush_events(&mixer);

    l = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT(l->clip == &clip_c);
    MEL_ASSERT_EQ(l->queue.count, (usize)0);

    mel_anim_mixer_update(&mixer, 0.1f);
    const Mel_Anim_Mixer_Output* out = mel_anim_mixer_find_output(&mixer, 1);
    MEL_ASSERT_NOT_NULL(out);
    MEL_ASSERT_FLOAT_EQ(out->value[0], 90.0f, 1.0f);

    mel_event_channel_off(&mixer.events, sub);
    mel_anim_clip_destroy(&clip_a, alloc);
    mel_anim_clip_destroy(&clip_b, alloc);
    mel_anim_clip_destroy(&clip_c, alloc);
    mel_anim_mixer_destroy(&mixer);
}