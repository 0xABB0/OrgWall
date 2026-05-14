#pragma once

#include <core/types.h>
#include "anim.pose.fwd.h"
#include "anim.clip.fwd.h"
#include "anim.skeleton.fwd.h"
#include <allocator/fwd.h>

struct Mel_Pose_Group {
    u64 type_hash;
    u32 count;
    u64* property_ids;
    void* data;
};

struct Mel_Local_Pose {
    Mel_Pose_Group* groups;
    u32 group_count;
};

void mel_pose_allocate(Mel_Local_Pose* out_pose,
                       const Mel_Anim_Clip* template_clip,
                       const Mel_Alloc* scratch);

void mel__pose_extract(const Mel_Local_Pose* pose, u64 type_hash, u64 property_id, void* out);

#define mel_pose_extract_float(pose, prop, out) mel__pose_extract((pose), MEL_ANIM_TYPE_F32,  (prop), (out))
#define mel_pose_extract_vec2(pose, prop, out)  mel__pose_extract((pose), MEL_ANIM_TYPE_VEC2, (prop), (out))
#define mel_pose_extract_vec3(pose, prop, out)  mel__pose_extract((pose), MEL_ANIM_TYPE_VEC3, (prop), (out))
#define mel_pose_extract_vec4(pose, prop, out)  mel__pose_extract((pose), MEL_ANIM_TYPE_VEC4, (prop), (out))
#define mel_pose_extract_quat(pose, prop, out)  mel__pose_extract((pose), MEL_ANIM_TYPE_QUAT, (prop), (out))

void mel_pose_calc_global_matrices(const Mel_Local_Pose* pose,
                                   const Mel_Skeleton* skeleton,
                                   f32* out_4x4_matrices);

void mel_pose_mesh_to_local(Mel_Local_Pose* additive_pose,
                            const Mel_Local_Pose* reference_pose,
                            const Mel_Skeleton* skeleton,
                            const Mel_Alloc* scratch);
