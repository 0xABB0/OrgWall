#pragma once

#include "core.types.h"
#include "anim.clip.fwd.h"
#include "anim.pose.fwd.h"
#include "allocator.fwd.h"

#define MEL_EASING_BEZIER MEL_EASING_COUNT

void mel_anim_sample(const Mel_Anim_Clip* clip,
                     f32 current_time,
                     u32* cursors,
                     const Mel_Alloc* scratch,
                     Mel_Local_Pose* out_pose);

void mel_anim_blend(Mel_Local_Pose* pose_a,
                    const Mel_Local_Pose* pose_b,
                    f32 weight,
                    const u64* mask_hashes,
                    u32 mask_count);

void mel_anim_blend_additive(Mel_Local_Pose* base_pose,
                             const Mel_Local_Pose* additive_pose,
                             const Mel_Local_Pose* reference_pose,
                             f32 weight,
                             const u64* mask_hashes,
                             u32 mask_count);
