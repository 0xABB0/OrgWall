#include "anim.pose.h"
#include "anim.clip.h"
#include "anim.skeleton.h"
#include "anim.registry.h"
#include "allocator.h"
#include "math.vec3.h"
#include "math.quat.h"
#include "math.mat4.h"

#include <string.h>

void mel_pose_allocate(Mel_Local_Pose* out_pose,
                       const Mel_Anim_Clip* template_clip,
                       const Mel_Alloc* scratch)
{
    assert(out_pose != NULL);
    assert(template_clip != NULL);
    assert(scratch != NULL);

    u32 gc = template_clip->group_count;
    out_pose->group_count = gc;
    out_pose->groups = mel_alloc_array(scratch, Mel_Pose_Group, gc);

    for (u32 g = 0; g < gc; g++)
    {
        const Mel_Track_Group* src = &template_clip->groups[g];
        Mel_Track_Type_Def* type_def = mel_anim_registry_get(src->type_hash);

        Mel_Pose_Group* dst = &out_pose->groups[g];
        dst->type_hash = src->type_hash;
        dst->count = src->track_count;
        dst->property_ids = mel_alloc_array(scratch, u64, src->track_count);
        memcpy(dst->property_ids, src->property_ids, sizeof(u64) * src->track_count);

        usize data_size = (usize)src->track_count * type_def->stride;
        dst->data = mel_aligned_alloc(scratch, data_size, 64);
        memset(dst->data, 0, data_size);
    }
}

void mel__pose_extract(const Mel_Local_Pose* pose, u64 type_hash, u64 property_id, void* out)
{
    assert(pose != NULL);
    assert(out != NULL);

    for (u32 g = 0; g < pose->group_count; g++)
    {
        const Mel_Pose_Group* grp = &pose->groups[g];
        if (grp->type_hash != type_hash) continue;

        Mel_Track_Type_Def* type_def = mel_anim_registry_get(grp->type_hash);
        u32 stride = type_def->stride;

        for (u32 i = 0; i < grp->count; i++)
        {
            if (grp->property_ids[i] == property_id)
            {
                memcpy(out, (const u8*)grp->data + (usize)i * stride, stride);
                return;
            }
        }
    }

    assert(false && "property not found in pose");
}

static i32 mel__pose_find_property(const Mel_Pose_Group* grp, u64 property_id)
{
    for (u32 i = 0; i < grp->count; i++)
    {
        if (grp->property_ids[i] == property_id)
            return (i32)i;
    }
    return -1;
}

static void mel__compute_global_transforms(const Mel_Local_Pose* pose,
                                            const Mel_Skeleton* skeleton,
                                            Mel_Vec3* out_positions,
                                            Mel_Quat* out_rotations)
{
    u64 type_vec3 = MEL_ANIM_TYPE_VEC3;
    u64 type_quat = MEL_ANIM_TYPE_QUAT;

    const Mel_Pose_Group* pos_group = NULL;
    const Mel_Pose_Group* rot_group = NULL;

    for (u32 g = 0; g < pose->group_count; g++)
    {
        if (pose->groups[g].type_hash == type_vec3 && !pos_group)
            pos_group = &pose->groups[g];
        else if (pose->groups[g].type_hash == type_quat)
            rot_group = &pose->groups[g];
    }

    for (u32 b = 0; b < skeleton->bone_count; b++)
    {
        u64 bone_hash = skeleton->bone_hashes[b];

        Mel_Vec3 local_pos = MEL_VEC3_ZERO;
        Mel_Quat local_rot = MEL_QUAT_IDENTITY;

        if (pos_group)
        {
            i32 idx = mel__pose_find_property(pos_group, bone_hash);
            if (idx >= 0)
                memcpy(&local_pos, (const u8*)pos_group->data + (usize)idx * sizeof(Mel_Vec3), sizeof(Mel_Vec3));
        }

        if (rot_group)
        {
            i32 idx = mel__pose_find_property(rot_group, bone_hash);
            if (idx >= 0)
                memcpy(&local_rot, (const u8*)rot_group->data + (usize)idx * sizeof(Mel_Quat), sizeof(Mel_Quat));
        }

        if (skeleton->parent_indices[b] < 0)
        {
            out_positions[b] = local_pos;
            out_rotations[b] = local_rot;
        }
        else
        {
            u32 p = (u32)skeleton->parent_indices[b];
            out_rotations[b] = mel_quat_mul(out_rotations[p], local_rot);
            out_positions[b] = mel_vec3_add(out_positions[p],
                                mel_quat_rotate_vec3(out_rotations[p], local_pos));
        }
    }
}

void mel_pose_mesh_to_local(Mel_Local_Pose* additive_pose,
                            const Mel_Local_Pose* reference_pose,
                            const Mel_Skeleton* skeleton,
                            const Mel_Alloc* scratch)
{
    assert(additive_pose != NULL);
    assert(reference_pose != NULL);
    assert(skeleton != NULL);
    assert(scratch != NULL);

    u32 bc = skeleton->bone_count;

    Mel_Vec3* add_global_pos = mel_alloc_array(scratch, Mel_Vec3, bc);
    Mel_Quat* add_global_rot = mel_alloc_array(scratch, Mel_Quat, bc);
    Mel_Vec3* ref_global_pos = mel_alloc_array(scratch, Mel_Vec3, bc);
    Mel_Quat* ref_global_rot = mel_alloc_array(scratch, Mel_Quat, bc);

    mel__compute_global_transforms(additive_pose, skeleton, add_global_pos, add_global_rot);
    mel__compute_global_transforms(reference_pose, skeleton, ref_global_pos, ref_global_rot);

    u64 type_vec3 = MEL_ANIM_TYPE_VEC3;
    u64 type_quat = MEL_ANIM_TYPE_QUAT;

    Mel_Pose_Group* add_pos_group = NULL;
    Mel_Pose_Group* add_rot_group = NULL;

    for (u32 g = 0; g < additive_pose->group_count; g++)
    {
        if (additive_pose->groups[g].type_hash == type_vec3 && !add_pos_group)
            add_pos_group = &additive_pose->groups[g];
        else if (additive_pose->groups[g].type_hash == type_quat)
            add_rot_group = &additive_pose->groups[g];
    }

    const Mel_Pose_Group* ref_pos_group = NULL;
    const Mel_Pose_Group* ref_rot_group = NULL;

    for (u32 g = 0; g < reference_pose->group_count; g++)
    {
        if (reference_pose->groups[g].type_hash == type_vec3 && !ref_pos_group)
            ref_pos_group = &reference_pose->groups[g];
        else if (reference_pose->groups[g].type_hash == type_quat)
            ref_rot_group = &reference_pose->groups[g];
    }

    for (u32 b = 0; b < bc; b++)
    {
        u64 bone_hash = skeleton->bone_hashes[b];
        Mel_Quat parent_ref_rot = MEL_QUAT_IDENTITY;
        if (skeleton->parent_indices[b] >= 0)
            parent_ref_rot = ref_global_rot[(u32)skeleton->parent_indices[b]];

        Mel_Quat inv_parent_ref_rot = mel_quat_inverse(parent_ref_rot);

        if (add_pos_group)
        {
            i32 idx = mel__pose_find_property(add_pos_group, bone_hash);
            if (idx >= 0)
            {
                Mel_Vec3 delta_mesh = mel_vec3_sub(add_global_pos[b], ref_global_pos[b]);
                Mel_Vec3 delta_local = mel_quat_rotate_vec3(inv_parent_ref_rot, delta_mesh);

                Mel_Vec3 ref_local = MEL_VEC3_ZERO;
                if (ref_pos_group)
                {
                    i32 ref_idx = mel__pose_find_property(ref_pos_group, bone_hash);
                    if (ref_idx >= 0)
                        memcpy(&ref_local, (const u8*)ref_pos_group->data + (usize)ref_idx * sizeof(Mel_Vec3), sizeof(Mel_Vec3));
                }

                Mel_Vec3 new_local = mel_vec3_add(ref_local, delta_local);
                memcpy((u8*)add_pos_group->data + (usize)idx * sizeof(Mel_Vec3), &new_local, sizeof(Mel_Vec3));
            }
        }

        if (add_rot_group)
        {
            i32 idx = mel__pose_find_property(add_rot_group, bone_hash);
            if (idx >= 0)
            {
                Mel_Quat delta_mesh = mel_quat_mul(add_global_rot[b], mel_quat_inverse(ref_global_rot[b]));
                Mel_Quat delta_local = mel_quat_mul(mel_quat_mul(inv_parent_ref_rot, delta_mesh), parent_ref_rot);

                Mel_Quat ref_local = MEL_QUAT_IDENTITY;
                if (ref_rot_group)
                {
                    i32 ref_idx = mel__pose_find_property(ref_rot_group, bone_hash);
                    if (ref_idx >= 0)
                        memcpy(&ref_local, (const u8*)ref_rot_group->data + (usize)ref_idx * sizeof(Mel_Quat), sizeof(Mel_Quat));
                }

                Mel_Quat new_local = mel_quat_mul(ref_local, delta_local);
                memcpy((u8*)add_rot_group->data + (usize)idx * sizeof(Mel_Quat), &new_local, sizeof(Mel_Quat));
            }
        }
    }
}

void mel_pose_calc_global_matrices(const Mel_Local_Pose* pose,
                                   const Mel_Skeleton* skeleton,
                                   f32* out_4x4_matrices)
{
    assert(pose != NULL);
    assert(skeleton != NULL);
    assert(out_4x4_matrices != NULL);

    u64 type_vec3 = MEL_ANIM_TYPE_VEC3;
    u64 type_quat = MEL_ANIM_TYPE_QUAT;

    const Mel_Pose_Group* pos_group = NULL;
    const Mel_Pose_Group* rot_group = NULL;
    const Mel_Pose_Group* scl_group = NULL;

    for (u32 g = 0; g < pose->group_count; g++)
    {
        if (pose->groups[g].type_hash == type_vec3)
        {
            if (!pos_group) pos_group = &pose->groups[g];
            else scl_group = &pose->groups[g];
        }
        else if (pose->groups[g].type_hash == type_quat)
        {
            rot_group = &pose->groups[g];
        }
    }

    assert(pos_group != NULL);
    assert(rot_group != NULL);

    for (u32 b = 0; b < skeleton->bone_count; b++)
    {
        u64 bone_hash = skeleton->bone_hashes[b];

        Mel_Vec3 pos = MEL_VEC3_ZERO;
        Mel_Quat rot = MEL_QUAT_IDENTITY;
        Mel_Vec3 scl = MEL_VEC3_ONE;

        for (u32 i = 0; i < pos_group->count; i++)
        {
            if (pos_group->property_ids[i] == bone_hash)
            {
                memcpy(&pos, (const u8*)pos_group->data + (usize)i * sizeof(Mel_Vec3), sizeof(Mel_Vec3));
                break;
            }
        }

        for (u32 i = 0; i < rot_group->count; i++)
        {
            if (rot_group->property_ids[i] == bone_hash)
            {
                memcpy(&rot, (const u8*)rot_group->data + (usize)i * sizeof(Mel_Quat), sizeof(Mel_Quat));
                break;
            }
        }

        if (scl_group)
        {
            for (u32 i = 0; i < scl_group->count; i++)
            {
                if (scl_group->property_ids[i] == bone_hash)
                {
                    memcpy(&scl, (const u8*)scl_group->data + (usize)i * sizeof(Mel_Vec3), sizeof(Mel_Vec3));
                    break;
                }
            }
        }

        Mel_Mat4 local = mel_mat4_mul(
            mel_mat4_mul(mel_mat4_translate(pos), mel_quat_to_mat4(rot)),
            mel_mat4_scale(scl)
        );

        Mel_Mat4* out = (Mel_Mat4*)(out_4x4_matrices + b * 16);

        if (skeleton->parent_indices[b] < 0)
        {
            *out = local;
        }
        else
        {
            Mel_Mat4* parent = (Mel_Mat4*)(out_4x4_matrices + skeleton->parent_indices[b] * 16);
            *out = mel_mat4_mul(*parent, local);
        }
    }
}
