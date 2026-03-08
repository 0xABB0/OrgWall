#include "../melody/test.harness.h"
#include "../melody/anim.registry.h"
#include "../melody/anim.clip.h"
#include "../melody/anim.pose.h"
#include "../melody/anim.pipeline.h"
#include "../melody/anim.skeleton.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
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

MEL_TEST(anim_blend_two_poses, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 prop = mel_xxh3_64("val", 3);

    f32 times_a[] = { 0.0f };
    f32 values_a[] = { 0.0f };
    Mel_Anim_Clip clip_a = mel__test_make_f32_clip(
        heap, mel_xxh3_64("a", 1), prop, times_a, values_a, 1, 0, 1.0f, false);

    f32 times_b[] = { 0.0f };
    f32 values_b[] = { 100.0f };
    Mel_Anim_Clip clip_b = mel__test_make_f32_clip(
        heap, mel_xxh3_64("b", 1), prop, times_b, values_b, 1, 0, 1.0f, false);

    Mel_Local_Pose pose_a, pose_b;
    mel_pose_allocate(&pose_a, &clip_a, heap);
    mel_pose_allocate(&pose_b, &clip_b, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&clip_a, 0.0f, cursors, heap, &pose_a);
    cursors[0] = 0;
    mel_anim_sample(&clip_b, 0.0f, cursors, heap, &pose_b);

    mel_anim_blend(&pose_a, &pose_b, 0.5f, NULL, 0);

    f32 result;
    mel_pose_extract_float(&pose_a, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 50.0f, 0.001f);

    mel_anim_clip_destroy(&clip_a, heap);
    mel_anim_clip_destroy(&clip_b, heap);
}

MEL_TEST(anim_blend_weight_zero_no_change, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 prop = mel_xxh3_64("val", 3);

    f32 times_a[] = { 0.0f };
    f32 values_a[] = { 42.0f };
    Mel_Anim_Clip clip_a = mel__test_make_f32_clip(
        heap, mel_xxh3_64("a", 1), prop, times_a, values_a, 1, 0, 1.0f, false);

    f32 times_b[] = { 0.0f };
    f32 values_b[] = { 999.0f };
    Mel_Anim_Clip clip_b = mel__test_make_f32_clip(
        heap, mel_xxh3_64("b", 1), prop, times_b, values_b, 1, 0, 1.0f, false);

    Mel_Local_Pose pose_a, pose_b;
    mel_pose_allocate(&pose_a, &clip_a, heap);
    mel_pose_allocate(&pose_b, &clip_b, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&clip_a, 0.0f, cursors, heap, &pose_a);
    cursors[0] = 0;
    mel_anim_sample(&clip_b, 0.0f, cursors, heap, &pose_b);

    mel_anim_blend(&pose_a, &pose_b, 0.0f, NULL, 0);

    f32 result;
    mel_pose_extract_float(&pose_a, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 42.0f, 0.001f);

    mel_anim_clip_destroy(&clip_a, heap);
    mel_anim_clip_destroy(&clip_b, heap);
}

MEL_TEST(anim_blend_weight_one_full_override, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 prop = mel_xxh3_64("val", 3);

    f32 times_a[] = { 0.0f };
    f32 values_a[] = { 42.0f };
    Mel_Anim_Clip clip_a = mel__test_make_f32_clip(
        heap, mel_xxh3_64("a", 1), prop, times_a, values_a, 1, 0, 1.0f, false);

    f32 times_b[] = { 0.0f };
    f32 values_b[] = { 100.0f };
    Mel_Anim_Clip clip_b = mel__test_make_f32_clip(
        heap, mel_xxh3_64("b", 1), prop, times_b, values_b, 1, 0, 1.0f, false);

    Mel_Local_Pose pose_a, pose_b;
    mel_pose_allocate(&pose_a, &clip_a, heap);
    mel_pose_allocate(&pose_b, &clip_b, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&clip_a, 0.0f, cursors, heap, &pose_a);
    cursors[0] = 0;
    mel_anim_sample(&clip_b, 0.0f, cursors, heap, &pose_b);

    mel_anim_blend(&pose_a, &pose_b, 1.0f, NULL, 0);

    f32 result;
    mel_pose_extract_float(&pose_a, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 100.0f, 0.001f);

    mel_anim_clip_destroy(&clip_a, heap);
    mel_anim_clip_destroy(&clip_b, heap);
}

MEL_TEST(anim_blend_with_mask, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 prop_x = mel_xxh3_64("x", 1);
    u64 prop_y = mel_xxh3_64("y", 1);

    Mel_Anim_Clip clip_a = {
        .name_hash = mel_xxh3_64("a", 1),
        .duration = 1.0f,
        .is_looping = false,
        .group_count = 1,
        .groups = mel_alloc_type(heap, Mel_Track_Group),
    };

    Mel_Track_Group* grp_a = &clip_a.groups[0];
    *grp_a = (Mel_Track_Group){0};
    grp_a->type_hash = MEL_ANIM_TYPE_F32;
    grp_a->track_count = 2;
    grp_a->property_ids = mel_alloc_array(heap, u64, 2);
    grp_a->property_ids[0] = prop_x;
    grp_a->property_ids[1] = prop_y;
    grp_a->keyframe_counts = mel_alloc_array(heap, u32, 2);
    grp_a->keyframe_counts[0] = 1;
    grp_a->keyframe_counts[1] = 1;
    grp_a->data_offsets = mel_alloc_array(heap, u32, 2);
    grp_a->data_offsets[0] = 0;
    grp_a->data_offsets[1] = 1;
    grp_a->flat_times = mel_alloc_array(heap, f32, 2);
    grp_a->flat_times[0] = 0.0f;
    grp_a->flat_times[1] = 0.0f;
    grp_a->flat_values = mel_alloc_array(heap, f32, 2);
    ((f32*)grp_a->flat_values)[0] = 10.0f;
    ((f32*)grp_a->flat_values)[1] = 20.0f;
    grp_a->flat_easing_ids = mel_alloc_array(heap, u16, 2);

    Mel_Anim_Clip clip_b = clip_a;
    clip_b.groups = mel_alloc_type(heap, Mel_Track_Group);
    *clip_b.groups = *grp_a;
    clip_b.groups->flat_values = mel_alloc_array(heap, f32, 2);
    ((f32*)clip_b.groups->flat_values)[0] = 100.0f;
    ((f32*)clip_b.groups->flat_values)[1] = 200.0f;
    clip_b.groups->property_ids = mel_alloc_array(heap, u64, 2);
    clip_b.groups->property_ids[0] = prop_x;
    clip_b.groups->property_ids[1] = prop_y;
    clip_b.groups->keyframe_counts = mel_alloc_array(heap, u32, 2);
    clip_b.groups->keyframe_counts[0] = 1;
    clip_b.groups->keyframe_counts[1] = 1;
    clip_b.groups->data_offsets = mel_alloc_array(heap, u32, 2);
    clip_b.groups->data_offsets[0] = 0;
    clip_b.groups->data_offsets[1] = 1;
    clip_b.groups->flat_times = mel_alloc_array(heap, f32, 2);
    clip_b.groups->flat_easing_ids = mel_alloc_array(heap, u16, 2);

    Mel_Local_Pose pose_a, pose_b;
    mel_pose_allocate(&pose_a, &clip_a, heap);
    mel_pose_allocate(&pose_b, &clip_b, heap);

    u32 cursors[2] = {0, 0};
    mel_anim_sample(&clip_a, 0.0f, cursors, heap, &pose_a);
    cursors[0] = 0; cursors[1] = 0;
    mel_anim_sample(&clip_b, 0.0f, cursors, heap, &pose_b);

    u64 mask[] = { prop_x };
    mel_anim_blend(&pose_a, &pose_b, 1.0f, mask, 1);

    f32 rx, ry;
    mel_pose_extract_float(&pose_a, prop_x, &rx);
    mel_pose_extract_float(&pose_a, prop_y, &ry);

    MEL_ASSERT_FLOAT_EQ(rx, 100.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(ry, 20.0f, 0.001f);

    mel_anim_clip_destroy(&clip_a, heap);
    mel_anim_clip_destroy(&clip_b, heap);
}

MEL_TEST(anim_blend_additive, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 prop = mel_xxh3_64("val", 3);

    f32 base_times[] = { 0.0f };
    f32 base_values[] = { 50.0f };
    Mel_Anim_Clip base_clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("base", 4), prop, base_times, base_values, 1, 0, 1.0f, false);

    f32 add_times[] = { 0.0f };
    f32 add_values[] = { 30.0f };
    Mel_Anim_Clip add_clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("add", 3), prop, add_times, add_values, 1, 0, 1.0f, false);

    f32 ref_times[] = { 0.0f };
    f32 ref_values[] = { 10.0f };
    Mel_Anim_Clip ref_clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("ref", 3), prop, ref_times, ref_values, 1, 0, 1.0f, false);

    Mel_Local_Pose base_pose, add_pose, ref_pose;
    mel_pose_allocate(&base_pose, &base_clip, heap);
    mel_pose_allocate(&add_pose, &add_clip, heap);
    mel_pose_allocate(&ref_pose, &ref_clip, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&base_clip, 0.0f, cursors, heap, &base_pose);
    cursors[0] = 0;
    mel_anim_sample(&add_clip, 0.0f, cursors, heap, &add_pose);
    cursors[0] = 0;
    mel_anim_sample(&ref_clip, 0.0f, cursors, heap, &ref_pose);

    mel_anim_blend_additive(&base_pose, &add_pose, &ref_pose, 1.0f, NULL, 0);

    f32 result;
    mel_pose_extract_float(&base_pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 70.0f, 0.001f);

    mel_anim_clip_destroy(&base_clip, heap);
    mel_anim_clip_destroy(&add_clip, heap);
    mel_anim_clip_destroy(&ref_clip, heap);
}

MEL_TEST(anim_blend_additive_half_weight, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 prop = mel_xxh3_64("val", 3);

    f32 base_times[] = { 0.0f };
    f32 base_values[] = { 50.0f };
    Mel_Anim_Clip base_clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("base", 4), prop, base_times, base_values, 1, 0, 1.0f, false);

    f32 add_times[] = { 0.0f };
    f32 add_values[] = { 30.0f };
    Mel_Anim_Clip add_clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("add", 3), prop, add_times, add_values, 1, 0, 1.0f, false);

    f32 ref_times[] = { 0.0f };
    f32 ref_values[] = { 10.0f };
    Mel_Anim_Clip ref_clip = mel__test_make_f32_clip(
        heap, mel_xxh3_64("ref", 3), prop, ref_times, ref_values, 1, 0, 1.0f, false);

    Mel_Local_Pose base_pose, add_pose, ref_pose;
    mel_pose_allocate(&base_pose, &base_clip, heap);
    mel_pose_allocate(&add_pose, &add_clip, heap);
    mel_pose_allocate(&ref_pose, &ref_clip, heap);

    u32 cursors[1] = {0};
    mel_anim_sample(&base_clip, 0.0f, cursors, heap, &base_pose);
    cursors[0] = 0;
    mel_anim_sample(&add_clip, 0.0f, cursors, heap, &add_pose);
    cursors[0] = 0;
    mel_anim_sample(&ref_clip, 0.0f, cursors, heap, &ref_pose);

    mel_anim_blend_additive(&base_pose, &add_pose, &ref_pose, 0.5f, NULL, 0);

    f32 result;
    mel_pose_extract_float(&base_pose, prop, &result);
    MEL_ASSERT_FLOAT_EQ(result, 60.0f, 0.001f);

    mel_anim_clip_destroy(&base_clip, heap);
    mel_anim_clip_destroy(&add_clip, heap);
    mel_anim_clip_destroy(&ref_clip, heap);
}

MEL_TEST(anim_pose_mesh_to_local, .tags = "anim")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    mel_anim_registry_init(heap);

    u64 root_hash = mel_xxh3_64("root", 4);
    u64 child_hash = mel_xxh3_64("child", 5);

    Mel_Skeleton skeleton = {
        .bone_count = 2,
        .bone_hashes = mel_alloc_array(heap, u64, 2),
        .parent_indices = mel_alloc_array(heap, i32, 2),
        .root_bone_hash = root_hash,
    };
    skeleton.bone_hashes[0] = root_hash;
    skeleton.bone_hashes[1] = child_hash;
    skeleton.parent_indices[0] = -1;
    skeleton.parent_indices[1] = 0;

    Mel_Anim_Clip clip = {
        .name_hash = mel_xxh3_64("pose", 4),
        .duration = 1.0f,
        .group_count = 2,
        .groups = mel_alloc_array(heap, Mel_Track_Group, 2),
    };

    Mel_Track_Group* pos_grp = &clip.groups[0];
    *pos_grp = (Mel_Track_Group){0};
    pos_grp->type_hash = MEL_ANIM_TYPE_VEC3;
    pos_grp->track_count = 2;
    pos_grp->property_ids = mel_alloc_array(heap, u64, 2);
    pos_grp->property_ids[0] = root_hash;
    pos_grp->property_ids[1] = child_hash;
    pos_grp->keyframe_counts = mel_alloc_array(heap, u32, 2);
    pos_grp->keyframe_counts[0] = 1;
    pos_grp->keyframe_counts[1] = 1;
    pos_grp->data_offsets = mel_alloc_array(heap, u32, 2);
    pos_grp->data_offsets[0] = 0;
    pos_grp->data_offsets[1] = 1;
    pos_grp->flat_times = mel_alloc_array(heap, f32, 2);
    pos_grp->flat_times[0] = 0.0f;
    pos_grp->flat_times[1] = 0.0f;
    pos_grp->flat_easing_ids = mel_alloc_array(heap, u16, 2);

    Mel_Track_Group* rot_grp = &clip.groups[1];
    *rot_grp = (Mel_Track_Group){0};
    rot_grp->type_hash = MEL_ANIM_TYPE_QUAT;
    rot_grp->track_count = 2;
    rot_grp->property_ids = mel_alloc_array(heap, u64, 2);
    rot_grp->property_ids[0] = root_hash;
    rot_grp->property_ids[1] = child_hash;
    rot_grp->keyframe_counts = mel_alloc_array(heap, u32, 2);
    rot_grp->keyframe_counts[0] = 1;
    rot_grp->keyframe_counts[1] = 1;
    rot_grp->data_offsets = mel_alloc_array(heap, u32, 2);
    rot_grp->data_offsets[0] = 0;
    rot_grp->data_offsets[1] = 1;
    rot_grp->flat_times = mel_alloc_array(heap, f32, 2);
    rot_grp->flat_times[0] = 0.0f;
    rot_grp->flat_times[1] = 0.0f;
    rot_grp->flat_easing_ids = mel_alloc_array(heap, u16, 2);

    Mel_Vec3 ref_root_pos = MEL_VEC3(0, 0, 0);
    Mel_Vec3 ref_child_pos = MEL_VEC3(1, 0, 0);
    Mel_Quat ref_root_rot = MEL_QUAT_IDENTITY;
    Mel_Quat ref_child_rot = MEL_QUAT_IDENTITY;

    Mel_Vec3 add_root_pos = MEL_VEC3(0, 0, 0);
    Mel_Vec3 add_child_pos = MEL_VEC3(1, 0, 0);
    Mel_Quat add_root_rot = MEL_QUAT_IDENTITY;
    Mel_Quat add_child_rot = mel_quat_from_axis_angle(MEL_VEC3(0, 0, 1), 0.5f);

    Mel_Local_Pose ref_pose, add_pose;
    mel_pose_allocate(&ref_pose, &clip, heap);
    mel_pose_allocate(&add_pose, &clip, heap);

    u32 cursors[4] = {0};

    u32 vec3_stride = sizeof(f32) * 3;
    u32 quat_stride = sizeof(f32) * 4;

    pos_grp->flat_values = mel_alloc(heap, (usize)vec3_stride * 2);
    memcpy((u8*)pos_grp->flat_values, ref_root_pos.e, vec3_stride);
    memcpy((u8*)pos_grp->flat_values + vec3_stride, ref_child_pos.e, vec3_stride);
    rot_grp->flat_values = mel_alloc(heap, (usize)quat_stride * 2);
    memcpy((u8*)rot_grp->flat_values, ref_root_rot.e, quat_stride);
    memcpy((u8*)rot_grp->flat_values + quat_stride, ref_child_rot.e, quat_stride);

    mel_anim_sample(&clip, 0.0f, cursors, heap, &ref_pose);

    memcpy((u8*)pos_grp->flat_values, add_root_pos.e, vec3_stride);
    memcpy((u8*)pos_grp->flat_values + vec3_stride, add_child_pos.e, vec3_stride);
    memcpy((u8*)rot_grp->flat_values, add_root_rot.e, quat_stride);
    memcpy((u8*)rot_grp->flat_values + quat_stride, add_child_rot.e, quat_stride);

    memset(cursors, 0, sizeof(cursors));
    mel_anim_sample(&clip, 0.0f, cursors, heap, &add_pose);

    mel_pose_mesh_to_local(&add_pose, &ref_pose, &skeleton, heap);

    Mel_Local_Pose base_pose;
    mel_pose_allocate(&base_pose, &clip, heap);

    memcpy((u8*)pos_grp->flat_values, ref_root_pos.e, vec3_stride);
    memcpy((u8*)pos_grp->flat_values + vec3_stride, ref_child_pos.e, vec3_stride);
    memcpy((u8*)rot_grp->flat_values, ref_root_rot.e, quat_stride);
    memcpy((u8*)rot_grp->flat_values + quat_stride, ref_child_rot.e, quat_stride);

    memset(cursors, 0, sizeof(cursors));
    mel_anim_sample(&clip, 0.0f, cursors, heap, &base_pose);

    mel_anim_blend_additive(&base_pose, &add_pose, &ref_pose, 1.0f, NULL, 0);

    Mel_Quat child_result;
    mel_pose_extract_quat(&base_pose, child_hash, &child_result);

    MEL_ASSERT_FLOAT_EQ(child_result.x, add_child_rot.x, 0.01f);
    MEL_ASSERT_FLOAT_EQ(child_result.y, add_child_rot.y, 0.01f);
    MEL_ASSERT_FLOAT_EQ(child_result.z, add_child_rot.z, 0.01f);
    MEL_ASSERT_FLOAT_EQ(child_result.w, add_child_rot.w, 0.01f);

    mel_anim_clip_destroy(&clip, heap);
}
