#include "../melody/test.harness.h"
#include "../melody/anim.registry.h"
#include "../melody/anim.clip.h"
#include "../melody/anim.pose.h"
#include "../melody/anim.pipeline.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/hash.xxh.h"
#include "../melody/math.easing.h"

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

MEL_TEST(anim_registry_init_and_get, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_Track_Type_Def* f32_def = mel_anim_registry_get(MEL_ANIM_TYPE_F32);
    MEL_ASSERT_NOT_NULL(f32_def);
    MEL_ASSERT_EQ(f32_def->stride, sizeof(f32));
    MEL_ASSERT_NOT_NULL(f32_def->lerp_fn);

    Mel_Track_Type_Def* vec3_def = mel_anim_registry_get(MEL_ANIM_TYPE_VEC3);
    MEL_ASSERT_NOT_NULL(vec3_def);
    MEL_ASSERT_EQ(vec3_def->stride, sizeof(f32) * 3);

    Mel_Track_Type_Def* quat_def = mel_anim_registry_get(MEL_ANIM_TYPE_QUAT);
    MEL_ASSERT_NOT_NULL(quat_def);
    MEL_ASSERT_EQ(quat_def->stride, sizeof(f32) * 4);
}

MEL_TEST(anim_registry_vec2_registered, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_Track_Type_Def* vec2_def = mel_anim_registry_get(MEL_ANIM_TYPE_VEC2);
    MEL_ASSERT_NOT_NULL(vec2_def);
    MEL_ASSERT_EQ(vec2_def->stride, sizeof(f32) * 2);
}

MEL_TEST(anim_clip_cursor_count, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 10.0f };
    u64 prop = mel_xxh3_64("x", 1);

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, false);

    MEL_ASSERT_EQ(mel_anim_clip_cursor_count(&clip), 1);

    mel_anim_clip_destroy(&clip, heap);
}

MEL_TEST(anim_sample_linear_midpoint, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 10.0f };
    u64 prop = mel_xxh3_64("val", 3);

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, false);

    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &clip, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&clip, 0.5f, cursors, heap, &pose);

    f32 result;
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 5.0f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
}

MEL_TEST(anim_sample_at_boundaries, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 10.0f };
    u64 prop = mel_xxh3_64("val", 3);

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, false);

    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &clip, heap);

    u32 cursors[1] = {0};

    mel_anim_sample(&clip, 0.0f, cursors, heap, &pose);
    f32 result;
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 0.0f, 0.001f);

    cursors[0] = 0;
    mel_anim_sample(&clip, 1.0f, cursors, heap, &pose);
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 10.0f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
}

MEL_TEST(anim_sample_stepped_easing, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    f32 times[] = { 0.0f, 0.5f, 1.0f };
    f32 values[] = { 0.0f, 5.0f, 10.0f };
    u64 prop = mel_xxh3_64("frame", 5);

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 3,
        MEL_EASING_COUNT - 1, 1.0f, false);

    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &clip, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&clip, 0.25f, cursors, heap, &pose);

    f32 result;
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 0.0f, 0.001f);

    cursors[0] = 0;
    mel_anim_sample(&clip, 0.75f, cursors, heap, &pose);
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 5.0f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
}

MEL_TEST(anim_sample_quarter_interpolation, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    f32 times[] = { 0.0f, 1.0f };
    f32 values[] = { 0.0f, 100.0f };
    u64 prop = mel_xxh3_64("val", 3);

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 2, 0, 1.0f, false);

    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &clip, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&clip, 0.25f, cursors, heap, &pose);
    f32 result;
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 25.0f, 0.001f);

    cursors[0] = 0;
    mel_anim_sample(&clip, 0.75f, cursors, heap, &pose);
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 75.0f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
}

MEL_TEST(anim_sample_multi_keyframe_segments, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    f32 times[] = { 0.0f, 0.5f, 1.0f };
    f32 values[] = { 0.0f, 100.0f, 0.0f };
    u64 prop = mel_xxh3_64("val", 3);

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 3, 0, 1.0f, false);

    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &clip, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&clip, 0.25f, cursors, heap, &pose);
    f32 result;
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 50.0f, 0.001f);

    cursors[0] = 0;
    mel_anim_sample(&clip, 0.75f, cursors, heap, &pose);
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 50.0f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
}

MEL_TEST(anim_sample_single_keyframe, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    f32 times[] = { 0.0f };
    f32 values[] = { 42.0f };
    u64 prop = mel_xxh3_64("val", 3);

    Mel_Anim_Clip clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("test", 4), prop, times, values, 1, 0, 1.0f, false);

    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &clip, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&clip, 0.5f, cursors, heap, &pose);
    f32 result;
    mel_pose_extract_float(&pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 42.0f, 0.001f);

    mel_anim_clip_destroy(&clip, heap);
}

MEL_TEST(anim_clip_cursor_count_multi_group, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    Mel_Anim_Clip clip = {
        .name_hash = mel_xxh3_64("test", 4),
        .duration = 1.0f,
        .group_count = 2,
        .groups = mel_alloc_array(heap, Mel_Track_Group, 2),
    };

    clip.groups[0] = (Mel_Track_Group){0};
    clip.groups[0].type_hash = MEL_ANIM_TYPE_F32;
    clip.groups[0].track_count = 2;
    clip.groups[0].keyframe_counts = mel_alloc_array(heap, u32, 2);
    clip.groups[0].keyframe_counts[0] = 3;
    clip.groups[0].keyframe_counts[1] = 2;
    clip.groups[0].property_ids = mel_alloc_array(heap, u64, 2);
    clip.groups[0].data_offsets = mel_alloc_array(heap, u32, 2);

    clip.groups[1] = (Mel_Track_Group){0};
    clip.groups[1].type_hash = MEL_ANIM_TYPE_VEC3;
    clip.groups[1].track_count = 1;
    clip.groups[1].keyframe_counts = mel_alloc_type(heap, u32);
    clip.groups[1].keyframe_counts[0] = 4;
    clip.groups[1].property_ids = mel_alloc_type(heap, u64);
    clip.groups[1].data_offsets = mel_alloc_type(heap, u32);

    MEL_ASSERT_EQ(mel_anim_clip_cursor_count(&clip), 3);

    mel_anim_clip_destroy(&clip, heap);
}
