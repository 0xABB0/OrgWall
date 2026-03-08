#include "../melody/test.harness.h"
#include "../melody/anim.registry.h"
#include "../melody/anim.clip.h"
#include "../melody/anim.pose.h"
#include "../melody/anim.pipeline.h"
#include "../melody/anim.player.h"
#include "../melody/anim.skeleton.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.slotmap.h"
#include "../melody/hash.xxh.h"
#include "../melody/math.vec3.h"
#include "../melody/math.quat.h"

#include <string.h>

static Mel_Anim_Clip mel__test_make_f32_clip(const Mel_Alloc* alloc,
                                              u64 name_hash,
                                              u64 property_id,
                                              const f32* times,
                                              const f32* values,
                                              u32 keyframe_count,
                                              u16 easing_id,
                                              f32 duration,
                                              bool is_looping)
{
    Mel_Anim_Clip clip = {
        .name_hash = name_hash,
        .duration = duration,
        .is_looping = is_looping,
        .additive_space = MEL_ANIM_ADDITIVE_LOCAL,
        .group_count = 1,
        .groups = mel_alloc_type(alloc, Mel_Track_Group),
        .event_group_count = 0,
        .event_groups = NULL,
    };

    Mel_Track_Group* grp = &clip.groups[0];
    *grp = (Mel_Track_Group){0};
    grp->type_hash = MEL_ANIM_TYPE_F32;
    grp->track_count = 1;
    grp->property_ids = mel_alloc_type(alloc, u64);
    grp->property_ids[0] = property_id;
    grp->keyframe_counts = mel_alloc_type(alloc, u32);
    grp->keyframe_counts[0] = keyframe_count;
    grp->data_offsets = mel_alloc_type(alloc, u32);
    grp->data_offsets[0] = 0;
    grp->flat_times = mel_alloc_array(alloc, f32, keyframe_count);
    memcpy(grp->flat_times, times, sizeof(f32) * keyframe_count);
    grp->flat_values = mel_alloc_array(alloc, f32, keyframe_count);
    memcpy(grp->flat_values, values, sizeof(f32) * keyframe_count);
    grp->flat_easing_ids = mel_alloc_array(alloc, u16, keyframe_count);
    for (u32 i = 0; i < keyframe_count; i++)
        grp->flat_easing_ids[i] = easing_id;
    grp->flat_easing_params = NULL;

    return clip;
}

MEL_TEST(anim_player_basic_playback, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 10.0f };
    u64 prop = mel_xxh3_64("val", 3);

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, false);

    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, handle);
    mel_anim_player_update(&player, 0.5f);

    f32 result;
    mel_anim_player_get_float(&player, prop, heap, &result);
    MEL_ASSERT_FLOAT_EQ(result, 5.0f, 0.001f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_player_crossfade, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);

    f32 times_a[] = { 0.0f };
    f32 values_a[] = { 0.0f };
    Mel_Anim_Clip clip_a = mel__test_make_f32_clip(
        heap, mel_xxh3_64("a", 1), prop, times_a, values_a, 1, 0, 1.0f, false);

    f32 times_b[] = { 0.0f };
    f32 values_b[] = { 100.0f };
    Mel_Anim_Clip clip_b = mel__test_make_f32_clip(
        heap, mel_xxh3_64("b", 1), prop, times_b, values_b, 1, 0, 1.0f, false);

    Mel_Anim_Clip_Handle ha = mel_slotmap_insert(&pool, &clip_a);
    Mel_Anim_Clip_Handle hb = mel_slotmap_insert(&pool, &clip_b);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, ha);
    mel_anim_player_update(&player, 0.0f);

    mel_anim_player_play(&player, hb, .crossfade = 1.0f);
    mel_anim_player_update(&player, 0.5f);

    f32 result;
    mel_anim_player_get_float(&player, prop, heap, &result);
    MEL_ASSERT_FLOAT_EQ(result, 50.0f, 0.001f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_player_crossfade_completion_prunes_chain, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);

    f32 times_a[] = { 0.0f };
    f32 values_a[] = { 0.0f };
    Mel_Anim_Clip clip_a = mel__test_make_f32_clip(
        heap, mel_xxh3_64("a", 1), prop, times_a, values_a, 1, 0, 1.0f, false);

    f32 times_b[] = { 0.0f };
    f32 values_b[] = { 100.0f };
    Mel_Anim_Clip clip_b = mel__test_make_f32_clip(
        heap, mel_xxh3_64("b", 1), prop, times_b, values_b, 1, 0, 1.0f, false);

    Mel_Anim_Clip_Handle ha = mel_slotmap_insert(&pool, &clip_a);
    Mel_Anim_Clip_Handle hb = mel_slotmap_insert(&pool, &clip_b);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, ha);
    mel_anim_player_update(&player, 0.0f);
    MEL_ASSERT_EQ(player.chain_count, 1);

    mel_anim_player_play(&player, hb, .crossfade = 0.5f);
    MEL_ASSERT_EQ(player.chain_count, 2);

    mel_anim_player_update(&player, 0.6f);
    MEL_ASSERT_EQ(player.chain_count, 1);

    f32 result;
    mel_anim_player_get_float(&player, prop, heap, &result);
    MEL_ASSERT_FLOAT_EQ(result, 100.0f, 0.001f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_player_play_replaces_chain, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);

    f32 times_a[] = { 0.0f };
    f32 values_a[] = { 0.0f };
    Mel_Anim_Clip clip_a = mel__test_make_f32_clip(
        heap, mel_xxh3_64("a", 1), prop, times_a, values_a, 1, 0, 1.0f, false);

    f32 times_b[] = { 0.0f };
    f32 values_b[] = { 100.0f };
    Mel_Anim_Clip clip_b = mel__test_make_f32_clip(
        heap, mel_xxh3_64("b", 1), prop, times_b, values_b, 1, 0, 1.0f, false);

    Mel_Anim_Clip_Handle ha = mel_slotmap_insert(&pool, &clip_a);
    Mel_Anim_Clip_Handle hb = mel_slotmap_insert(&pool, &clip_b);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, ha);
    mel_anim_player_update(&player, 0.5f);

    mel_anim_player_play(&player, hb);
    MEL_ASSERT_EQ(player.chain_count, 1);

    mel_anim_player_update(&player, 0.0f);

    f32 result;
    mel_anim_player_get_float(&player, prop, heap, &result);
    MEL_ASSERT_FLOAT_EQ(result, 100.0f, 0.001f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_player_speed, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);
    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 10.0f };

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, false);

    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play_opt(&player, handle, (Mel_Anim_Play_Opt){ .speed = 2.0f });
    mel_anim_player_update(&player, 0.25f);

    f32 result;
    mel_anim_player_get_float(&player, prop, heap, &result);
    MEL_ASSERT_FLOAT_EQ(result, 5.0f, 0.001f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_player_looping, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);
    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 10.0f };

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, true);

    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, handle);
    mel_anim_player_update(&player, 1.5f);

    f32 result;
    mel_anim_player_get_float(&player, prop, heap, &result);
    MEL_ASSERT_FLOAT_EQ(result, 5.0f, 0.001f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_player_non_looping_clamps, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);
    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 10.0f };

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, false);

    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, handle);
    mel_anim_player_update(&player, 5.0f);

    f32 result;
    mel_anim_player_get_float(&player, prop, heap, &result);
    MEL_ASSERT_FLOAT_EQ(result, 10.0f, 0.001f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_player_phase_tracking, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);
    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 10.0f };

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, false);

    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, handle);
    mel_anim_player_update(&player, 0.25f);
    MEL_ASSERT_FLOAT_EQ(player.phase, 0.25f, 0.001f);

    mel_anim_player_update(&player, 0.25f);
    MEL_ASSERT_FLOAT_EQ(player.phase, 0.50f, 0.001f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_events_sorted_by_time, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);
    u64 sfx_channel = mel_xxh3_64("sfx", 3);

    Mel_Anim_Clip clip = {
        .name_hash = mel_xxh3_64("test", 4),
        .duration = 1.0f,
        .is_looping = false,
        .group_count = 1,
        .groups = mel_alloc_type(heap, Mel_Track_Group),
        .event_group_count = 1,
        .event_groups = mel_alloc_type(heap, Mel_Event_Group),
    };

    Mel_Track_Group* grp = &clip.groups[0];
    *grp = (Mel_Track_Group){0};
    grp->type_hash = MEL_ANIM_TYPE_F32;
    grp->track_count = 1;
    grp->property_ids = mel_alloc_type(heap, u64);
    grp->property_ids[0] = prop;
    grp->keyframe_counts = mel_alloc_type(heap, u32);
    grp->keyframe_counts[0] = 2;
    grp->data_offsets = mel_alloc_type(heap, u32);
    grp->data_offsets[0] = 0;
    grp->flat_times = mel_alloc_array(heap, f32, 2);
    grp->flat_times[0] = 0.0f;
    grp->flat_times[1] = 1.0f;
    grp->flat_values = mel_alloc_array(heap, f32, 2);
    ((f32*)grp->flat_values)[0] = 0.0f;
    ((f32*)grp->flat_values)[1] = 1.0f;
    grp->flat_easing_ids = mel_alloc_array(heap, u16, 2);

    u64 ev_a = mel_xxh3_64("hit", 3);
    u64 ev_b = mel_xxh3_64("step", 4);

    Mel_Event_Group* egrp = &clip.event_groups[0];
    *egrp = (Mel_Event_Group){0};
    egrp->track_count = 1;
    egrp->property_ids = mel_alloc_type(heap, u64);
    egrp->property_ids[0] = sfx_channel;
    egrp->keyframe_counts = mel_alloc_type(heap, u32);
    egrp->keyframe_counts[0] = 2;
    egrp->data_offsets = mel_alloc_type(heap, u32);
    egrp->data_offsets[0] = 0;
    egrp->flat_times = mel_alloc_array(heap, f32, 2);
    egrp->flat_times[0] = 0.7f;
    egrp->flat_times[1] = 0.3f;
    egrp->flat_event_hashes = mel_alloc_array(heap, u64, 2);
    egrp->flat_event_hashes[0] = ev_a;
    egrp->flat_event_hashes[1] = ev_b;

    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, handle);
    mel_anim_player_update(&player, 1.0f);

    MEL_ASSERT_EQ(player.event_count, 2);
    MEL_ASSERT(player.pending_events[0].time <= player.pending_events[1].time);
    MEL_ASSERT(player.pending_events[0].event_hash == ev_b);
    MEL_ASSERT(player.pending_events[1].event_hash == ev_a);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_events_cleared_each_update, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));

    u64 prop = mel_xxh3_64("val", 3);
    u64 sfx_channel = mel_xxh3_64("sfx", 3);
    u64 ev_hash = mel_xxh3_64("hit", 3);

    Mel_Anim_Clip clip = {
        .name_hash = mel_xxh3_64("test", 4),
        .duration = 1.0f,
        .is_looping = false,
        .group_count = 1,
        .groups = mel_alloc_type(heap, Mel_Track_Group),
        .event_group_count = 1,
        .event_groups = mel_alloc_type(heap, Mel_Event_Group),
    };

    Mel_Track_Group* grp = &clip.groups[0];
    *grp = (Mel_Track_Group){0};
    grp->type_hash = MEL_ANIM_TYPE_F32;
    grp->track_count = 1;
    grp->property_ids = mel_alloc_type(heap, u64);
    grp->property_ids[0] = prop;
    grp->keyframe_counts = mel_alloc_type(heap, u32);
    grp->keyframe_counts[0] = 2;
    grp->data_offsets = mel_alloc_type(heap, u32);
    grp->data_offsets[0] = 0;
    grp->flat_times = mel_alloc_array(heap, f32, 2);
    grp->flat_times[0] = 0.0f;
    grp->flat_times[1] = 1.0f;
    grp->flat_values = mel_alloc_array(heap, f32, 2);
    grp->flat_easing_ids = mel_alloc_array(heap, u16, 2);

    Mel_Event_Group* egrp = &clip.event_groups[0];
    *egrp = (Mel_Event_Group){0};
    egrp->track_count = 1;
    egrp->property_ids = mel_alloc_type(heap, u64);
    egrp->property_ids[0] = sfx_channel;
    egrp->keyframe_counts = mel_alloc_type(heap, u32);
    egrp->keyframe_counts[0] = 1;
    egrp->data_offsets = mel_alloc_type(heap, u32);
    egrp->data_offsets[0] = 0;
    egrp->flat_times = mel_alloc_type(heap, f32);
    egrp->flat_times[0] = 0.5f;
    egrp->flat_event_hashes = mel_alloc_type(heap, u64);
    egrp->flat_event_hashes[0] = ev_hash;

    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, handle);

    mel_anim_player_update(&player, 0.3f);
    MEL_ASSERT_EQ(player.event_count, 0);

    mel_anim_player_update(&player, 0.3f);
    MEL_ASSERT_EQ(player.event_count, 1);

    mel_anim_player_update(&player, 0.1f);
    MEL_ASSERT_EQ(player.event_count, 0);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}

MEL_TEST(anim_player_root_motion, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 root_hash = mel_xxh3_64("root", 4);

    Mel_Skeleton skeleton = {
        .bone_count = 1,
        .bone_hashes = mel_alloc_type(heap, u64),
        .parent_indices = mel_alloc_type(heap, i32),
        .root_bone_hash = root_hash,
    };
    skeleton.bone_hashes[0] = root_hash;
    skeleton.parent_indices[0] = -1;

    Mel_Anim_Clip clip = {
        .name_hash = mel_xxh3_64("walk", 4),
        .duration = 1.0f,
        .is_looping = false,
        .group_count = 2,
        .groups = mel_alloc_array(heap, Mel_Track_Group, 2),
    };

    u32 vec3_stride = sizeof(f32) * 3;
    u32 quat_stride = sizeof(f32) * 4;

    Mel_Track_Group* pos_grp = &clip.groups[0];
    *pos_grp = (Mel_Track_Group){0};
    pos_grp->type_hash = MEL_ANIM_TYPE_VEC3;
    pos_grp->track_count = 1;
    pos_grp->property_ids = mel_alloc_type(heap, u64);
    pos_grp->property_ids[0] = root_hash;
    pos_grp->keyframe_counts = mel_alloc_type(heap, u32);
    pos_grp->keyframe_counts[0] = 2;
    pos_grp->data_offsets = mel_alloc_type(heap, u32);
    pos_grp->data_offsets[0] = 0;
    pos_grp->flat_times = mel_alloc_array(heap, f32, 2);
    pos_grp->flat_times[0] = 0.0f;
    pos_grp->flat_times[1] = 1.0f;
    pos_grp->flat_easing_ids = mel_alloc_array(heap, u16, 2);
    pos_grp->flat_values = mel_alloc(heap, (usize)vec3_stride * 2);
    Mel_Vec3 pos0 = MEL_VEC3(0, 0, 0);
    Mel_Vec3 pos1 = MEL_VEC3(10, 0, 0);
    memcpy((u8*)pos_grp->flat_values, pos0.e, vec3_stride);
    memcpy((u8*)pos_grp->flat_values + vec3_stride, pos1.e, vec3_stride);

    Mel_Track_Group* rot_grp = &clip.groups[1];
    *rot_grp = (Mel_Track_Group){0};
    rot_grp->type_hash = MEL_ANIM_TYPE_QUAT;
    rot_grp->track_count = 1;
    rot_grp->property_ids = mel_alloc_type(heap, u64);
    rot_grp->property_ids[0] = root_hash;
    rot_grp->keyframe_counts = mel_alloc_type(heap, u32);
    rot_grp->keyframe_counts[0] = 1;
    rot_grp->data_offsets = mel_alloc_type(heap, u32);
    rot_grp->data_offsets[0] = 0;
    rot_grp->flat_times = mel_alloc_type(heap, f32);
    rot_grp->flat_times[0] = 0.0f;
    rot_grp->flat_easing_ids = mel_alloc_type(heap, u16);
    rot_grp->flat_values = mel_alloc(heap, (usize)quat_stride);
    Mel_Quat identity = MEL_QUAT_IDENTITY;
    memcpy(rot_grp->flat_values, identity.e, quat_stride);

    Mel_SlotMap pool;
    mel_slotmap_init(&pool, heap, .item_size = sizeof(Mel_Anim_Clip));
    Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&pool, &clip);

    Mel_Anim_Player player;
    mel_anim_player_init(&player, heap, &pool);
    mel_anim_player_play(&player, handle);

    mel_anim_player_update(&player, 0.5f);
    Mel_Local_Pose pose;
    mel_anim_player_sample(&player, heap, &pose);

    Mel_Root_Motion rm;
    mel_anim_player_extract_root_motion(&player, &pose, &skeleton, &rm);
    MEL_ASSERT_FLOAT_EQ(rm.delta_position.x, 5.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(rm.delta_position.y, 0.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(rm.delta_position.z, 0.0f, 0.01f);

    mel_anim_player_update(&player, 0.25f);
    mel_anim_player_sample(&player, heap, &pose);
    mel_anim_player_extract_root_motion(&player, &pose, &skeleton, &rm);
    MEL_ASSERT_FLOAT_EQ(rm.delta_position.x, 2.5f, 0.01f);

    mel_anim_player_destroy(&player);
    mel_slotmap_free(&pool);
}
