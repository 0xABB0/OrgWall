#include "anim.state.h"
#include "anim.mixer.h"
#include "anim.clip.h"
#include "anim.track.h"
#include "math.curve.h"
#include "allocator.heap.h"
#include "collection.array.h"
#include "test.harness.h"

static Mel_Anim_Clip make_clip(const Mel_Alloc* alloc, f32 duration, bool loop)
{
    Mel_Anim_Clip clip;
    mel_anim_clip_init(&clip, alloc, 0, 1, 0, duration, loop);

    Mel_Anim_Track* track = mel_anim_clip_track(&clip, 0);
    mel_anim_track_init(track, alloc, 1, MEL_ANIM_TRACK_F32, 2);

    f32 v0 = 0.0f, v1 = 1.0f;
    mel_anim_track_set_keyframe(track, 0, 0.0f, &v0, MEL_CURVE_LINEAR);
    mel_anim_track_set_keyframe(track, 1, duration, &v1, MEL_CURVE_LINEAR);

    return clip;
}

MEL_TEST(init_enters_initial_state, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Anim_Clip clips[2];
    clips[0] = make_clip(alloc, 1.0f, true);
    clips[1] = make_clip(alloc, 1.0f, true);

    Mel_Anim_State_Def states[] = {
        { .name_hash = 0xA, .clip = &clips[0], .transitions = NULL, .transition_count = 0 },
        { .name_hash = 0xB, .clip = &clips[1], .transitions = NULL, .transition_count = 0 },
    };

    Mel_Anim_State_Machine machine;
    mel_anim_state_machine_init(&machine, states, 2, 0);

    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_State_Player player;
    mel_anim_state_player_init(&player, alloc, .machine = &machine, .mixer = &mixer);

    MEL_ASSERT_EQ(mel_anim_state_player_current_state(&player), 0u);

    Mel_Anim_Layer* layer = mel_anim_mixer_layer(&mixer, 0);
    MEL_ASSERT(layer->playing);
    MEL_ASSERT(layer->clip == &clips[0]);

    mel_anim_state_player_destroy(&player);
    mel_anim_mixer_destroy(&mixer);
    mel_anim_clip_destroy(&clips[0], alloc);
    mel_anim_clip_destroy(&clips[1], alloc);
}

MEL_TEST(immediate_transition, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Anim_Clip clips[2];
    clips[0] = make_clip(alloc, 1.0f, true);
    clips[1] = make_clip(alloc, 1.0f, true);

    Mel_Anim_Transition trans = {
        .target_state = 1,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = 0x1,
        .mix_duration = 0.0f,
        .auto_clear_condition = false,
    };

    Mel_Anim_State_Def states[] = {
        { .name_hash = 0xA, .clip = &clips[0], .transitions = &trans, .transition_count = 1 },
        { .name_hash = 0xB, .clip = &clips[1], .transitions = NULL, .transition_count = 0 },
    };

    Mel_Anim_State_Machine machine;
    mel_anim_state_machine_init(&machine, states, 2, 0);

    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_State_Player player;
    mel_anim_state_player_init(&player, alloc, .machine = &machine, .mixer = &mixer);

    mel_anim_state_player_set_condition(&player, 0x1, true);
    mel_anim_state_player_update(&player);

    MEL_ASSERT_EQ(mel_anim_state_player_current_state(&player), 1u);

    mel_anim_state_player_destroy(&player);
    mel_anim_mixer_destroy(&mixer);
    mel_anim_clip_destroy(&clips[0], alloc);
    mel_anim_clip_destroy(&clips[1], alloc);
}

MEL_TEST(at_end_waits, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Anim_Clip clips[2];
    clips[0] = make_clip(alloc, 1.0f, false);
    clips[1] = make_clip(alloc, 1.0f, true);

    Mel_Anim_Transition trans = {
        .target_state = 1,
        .mode = MEL_ANIM_TRANSITION_AT_END,
        .condition_hash = 0x1,
        .mix_duration = 0.0f,
        .auto_clear_condition = false,
    };

    Mel_Anim_State_Def states[] = {
        { .name_hash = 0xA, .clip = &clips[0], .transitions = &trans, .transition_count = 1 },
        { .name_hash = 0xB, .clip = &clips[1], .transitions = NULL, .transition_count = 0 },
    };

    Mel_Anim_State_Machine machine;
    mel_anim_state_machine_init(&machine, states, 2, 0);

    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_State_Player player;
    mel_anim_state_player_init(&player, alloc, .machine = &machine, .mixer = &mixer);

    mel_anim_state_player_set_condition(&player, 0x1, true);

    mel_anim_mixer_update(&mixer, 0.5f);
    mel_anim_state_player_update(&player);
    MEL_ASSERT_EQ(mel_anim_state_player_current_state(&player), 0u);

    mel_anim_mixer_update(&mixer, 0.6f);
    mel_anim_state_player_update(&player);
    MEL_ASSERT_EQ(mel_anim_state_player_current_state(&player), 1u);

    mel_anim_state_player_destroy(&player);
    mel_anim_mixer_destroy(&mixer);
    mel_anim_clip_destroy(&clips[0], alloc);
    mel_anim_clip_destroy(&clips[1], alloc);
}

MEL_TEST(auto_clear_condition, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Anim_Clip clips[2];
    clips[0] = make_clip(alloc, 1.0f, true);
    clips[1] = make_clip(alloc, 1.0f, true);

    Mel_Anim_Transition trans = {
        .target_state = 1,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = 0x1,
        .mix_duration = 0.0f,
        .auto_clear_condition = true,
    };

    Mel_Anim_State_Def states[] = {
        { .name_hash = 0xA, .clip = &clips[0], .transitions = &trans, .transition_count = 1 },
        { .name_hash = 0xB, .clip = &clips[1], .transitions = NULL, .transition_count = 0 },
    };

    Mel_Anim_State_Machine machine;
    mel_anim_state_machine_init(&machine, states, 2, 0);

    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_State_Player player;
    mel_anim_state_player_init(&player, alloc, .machine = &machine, .mixer = &mixer);

    mel_anim_state_player_set_condition(&player, 0x1, true);
    MEL_ASSERT(mel_anim_state_player_has_condition(&player, 0x1));

    mel_anim_state_player_update(&player);
    MEL_ASSERT_EQ(mel_anim_state_player_current_state(&player), 1u);
    MEL_ASSERT(!mel_anim_state_player_has_condition(&player, 0x1));

    mel_anim_state_player_destroy(&player);
    mel_anim_mixer_destroy(&mixer);
    mel_anim_clip_destroy(&clips[0], alloc);
    mel_anim_clip_destroy(&clips[1], alloc);
}

static u32 s_transition_from = 0;
static u32 s_transition_to = 0;
static u32 s_transition_count = 0;

static void on_transition_cb(void* ctx, u32 from, u32 to)
{
    MEL_UNUSED(ctx);
    s_transition_from = from;
    s_transition_to = to;
    s_transition_count++;
}

MEL_TEST(on_transition_callback, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Anim_Clip clips[2];
    clips[0] = make_clip(alloc, 1.0f, true);
    clips[1] = make_clip(alloc, 1.0f, true);

    Mel_Anim_Transition trans = {
        .target_state = 1,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = 0x1,
        .mix_duration = 0.0f,
        .auto_clear_condition = false,
    };

    Mel_Anim_State_Def states[] = {
        { .name_hash = 0xA, .clip = &clips[0], .transitions = &trans, .transition_count = 1 },
        { .name_hash = 0xB, .clip = &clips[1], .transitions = NULL, .transition_count = 0 },
    };

    Mel_Anim_State_Machine machine;
    mel_anim_state_machine_init(&machine, states, 2, 0);

    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    s_transition_count = 0;
    Mel_Anim_State_Player player;
    mel_anim_state_player_init(&player, alloc,
        .machine = &machine,
        .mixer = &mixer,
        .on_transition = on_transition_cb);

    mel_anim_state_player_set_condition(&player, 0x1, true);
    mel_anim_state_player_update(&player);

    MEL_ASSERT_EQ(s_transition_count, 1u);
    MEL_ASSERT_EQ(s_transition_from, 0u);
    MEL_ASSERT_EQ(s_transition_to, 1u);

    mel_anim_state_player_destroy(&player);
    mel_anim_mixer_destroy(&mixer);
    mel_anim_clip_destroy(&clips[0], alloc);
    mel_anim_clip_destroy(&clips[1], alloc);
}

MEL_TEST(find_state_by_name_hash, .tags = "anim")
{
    Mel_Anim_State_Def states[] = {
        { .name_hash = 0xAA, .clip = NULL, .transitions = NULL, .transition_count = 0 },
        { .name_hash = 0xBB, .clip = NULL, .transitions = NULL, .transition_count = 0 },
        { .name_hash = 0xCC, .clip = NULL, .transitions = NULL, .transition_count = 0 },
    };

    Mel_Anim_State_Machine machine;
    mel_anim_state_machine_init(&machine, states, 3, 0);

    MEL_ASSERT_EQ(mel_anim_state_machine_find_state(&machine, 0xAA), 0u);
    MEL_ASSERT_EQ(mel_anim_state_machine_find_state(&machine, 0xBB), 1u);
    MEL_ASSERT_EQ(mel_anim_state_machine_find_state(&machine, 0xCC), 2u);

}

MEL_TEST(set_condition_add_remove, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Anim_Clip clip = make_clip(alloc, 1.0f, true);

    Mel_Anim_State_Def states[] = {
        { .name_hash = 0, .clip = &clip, .transitions = NULL, .transition_count = 0 },
    };

    Mel_Anim_State_Machine machine;
    mel_anim_state_machine_init(&machine, states, 1, 0);

    Mel_Anim_Mixer mixer;
    mel_anim_mixer_init(&mixer, alloc);
    mel_anim_mixer_add_layer(&mixer);

    Mel_Anim_State_Player player;
    mel_anim_state_player_init(&player, alloc, .machine = &machine, .mixer = &mixer);

    MEL_ASSERT(!mel_anim_state_player_has_condition(&player, 0x1));

    mel_anim_state_player_set_condition(&player, 0x1, true);
    MEL_ASSERT(mel_anim_state_player_has_condition(&player, 0x1));

    mel_anim_state_player_set_condition(&player, 0x2, true);
    MEL_ASSERT(mel_anim_state_player_has_condition(&player, 0x1));
    MEL_ASSERT(mel_anim_state_player_has_condition(&player, 0x2));

    mel_anim_state_player_set_condition(&player, 0x1, false);
    MEL_ASSERT(!mel_anim_state_player_has_condition(&player, 0x1));
    MEL_ASSERT(mel_anim_state_player_has_condition(&player, 0x2));

    mel_anim_state_player_destroy(&player);
    mel_anim_mixer_destroy(&mixer);
    mel_anim_clip_destroy(&clip, alloc);
}