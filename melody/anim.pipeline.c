#include "anim.pipeline.h"
#include "anim.clip.h"
#include "anim.pose.h"
#include "anim.registry.h"
#include "allocator.h"
#include "math.easing.h"
#include "math.curve.h"

#include <string.h>

static const Mel_Easing_Func mel__easing_table[] = {
    #define X(name, fn) fn,
    MEL_EASING_LIST(X)
    #undef X
};
_Static_assert(sizeof(mel__easing_table) / sizeof(mel__easing_table[0]) == MEL_EASING_COUNT,
               "easing table size mismatch");

static f32 mel__resolve_easing(u16 easing_id, f32 t,
                               const f32* easing_params, u32 keyframe_index)
{
    if (easing_id < MEL_EASING_COUNT)
        return mel__easing_table[easing_id](t);

    if (easing_id == MEL_EASING_BEZIER && easing_params)
    {
        const f32* p = &easing_params[keyframe_index * 4];
        f32 cx1 = p[0], cy1 = p[1], cx2 = p[2], cy2 = p[3];

        Mel_Bezier bez;
        mel_bezier_init(&bez, cx1, cy1, cx2, cy2);
        return mel_curve_eval(MEL_CURVE_BEZIER, t, &bez);
    }

    return t;
}

void mel_anim_sample(const Mel_Anim_Clip* clip,
                     f32 current_time,
                     u32* cursors,
                     const Mel_Alloc* scratch,
                     Mel_Local_Pose* out_pose)
{
    assert(clip != NULL);
    assert(cursors != NULL);
    assert(scratch != NULL);
    assert(out_pose != NULL);

    u32 cursor_offset = 0;

    for (u32 g = 0; g < clip->group_count; g++)
    {
        const Mel_Track_Group* grp = &clip->groups[g];
        Mel_Track_Type_Def* type_def = mel_anim_registry_get(grp->type_hash);
        Mel_Pose_Group* pose_grp = &out_pose->groups[g];

        u32 tc = grp->track_count;
        u32 stride = type_def->stride;
        u32 floats_per = stride / sizeof(f32);

        f32* buf_a = mel_alloc(scratch, (usize)tc * stride);
        f32* buf_b = mel_alloc(scratch, (usize)tc * stride);
        f32* buf_t = mel_alloc_array(scratch, f32, tc);

        for (u32 t = 0; t < tc; t++)
        {
            u32 kf_count = grp->keyframe_counts[t];
            u32 data_off = grp->data_offsets[t];
            const f32* times = &grp->flat_times[data_off];
            const u8* values = (const u8*)grp->flat_values + (usize)data_off * stride;
            u32* cur = &cursors[cursor_offset + t];

            if (kf_count == 1 || current_time <= times[0])
            {
                memcpy(&buf_a[t * floats_per], values, stride);
                memcpy(&buf_b[t * floats_per], values, stride);
                buf_t[t] = 0.0f;
                *cur = 0;
                continue;
            }

            u32 last = kf_count - 1;
            if (current_time >= times[last])
            {
                const u8* last_val = values + (usize)last * stride;
                memcpy(&buf_a[t * floats_per], last_val, stride);
                memcpy(&buf_b[t * floats_per], last_val, stride);
                buf_t[t] = 0.0f;
                *cur = last;
                continue;
            }

            u32 left = *cur;
            if (left >= kf_count) left = 0;
            if (current_time < times[left]) left = 0;

            while (left + 1 < kf_count && times[left + 1] <= current_time)
                left++;

            *cur = left;
            u32 right = left + 1;

            f32 span = times[right] - times[left];
            f32 t_local = (span > 1e-7f) ? (current_time - times[left]) / span : 0.0f;

            u16 easing_id = grp->flat_easing_ids[data_off + left];
            f32 t_eased = mel__resolve_easing(easing_id, t_local,
                                              grp->flat_easing_params,
                                              data_off + left);

            memcpy(&buf_a[t * floats_per], values + (usize)left * stride, stride);
            memcpy(&buf_b[t * floats_per], values + (usize)right * stride, stride);
            buf_t[t] = t_eased;
        }

        type_def->lerp_fn(buf_a, buf_b, pose_grp->data, buf_t, tc);
        cursor_offset += tc;
    }
}

void mel_anim_blend(Mel_Local_Pose* pose_a,
                    const Mel_Local_Pose* pose_b,
                    f32 weight,
                    const u64* mask_hashes,
                    u32 mask_count)
{
    assert(pose_a != NULL);
    assert(pose_b != NULL);

    if (weight <= 0.0f) return;
    if (weight > 1.0f) weight = 1.0f;

    for (u32 ga = 0; ga < pose_a->group_count; ga++)
    {
        Mel_Pose_Group* grp_a = &pose_a->groups[ga];

        const Mel_Pose_Group* grp_b = NULL;
        for (u32 gb = 0; gb < pose_b->group_count; gb++)
        {
            if (pose_b->groups[gb].type_hash == grp_a->type_hash)
            {
                grp_b = &pose_b->groups[gb];
                break;
            }
        }
        if (!grp_b) continue;

        Mel_Track_Type_Def* type_def = mel_anim_registry_get(grp_a->type_hash);
        u32 stride = type_def->stride;

        for (u32 ia = 0; ia < grp_a->count; ia++)
        {
            u64 prop = grp_a->property_ids[ia];

            if (mask_hashes)
            {
                bool found = false;
                for (u32 m = 0; m < mask_count; m++)
                {
                    if (mask_hashes[m] == prop) { found = true; break; }
                }
                if (!found) continue;
            }

            for (u32 ib = 0; ib < grp_b->count; ib++)
            {
                if (grp_b->property_ids[ib] != prop) continue;

                u8* dst = (u8*)grp_a->data + (usize)ia * stride;
                const u8* src = (const u8*)grp_b->data + (usize)ib * stride;

                type_def->lerp_fn(dst, src, dst, &weight, 1);
                break;
            }
        }
    }
}

void mel_anim_blend_additive(Mel_Local_Pose* base_pose,
                             const Mel_Local_Pose* additive_pose,
                             const Mel_Local_Pose* reference_pose,
                             f32 weight,
                             const u64* mask_hashes,
                             u32 mask_count)
{
    assert(base_pose != NULL);
    assert(additive_pose != NULL);
    assert(reference_pose != NULL);

    if (weight <= 0.0f) return;

    for (u32 gb = 0; gb < base_pose->group_count; gb++)
    {
        Mel_Pose_Group* grp_base = &base_pose->groups[gb];

        const Mel_Pose_Group* grp_add = NULL;
        const Mel_Pose_Group* grp_ref = NULL;

        for (u32 g = 0; g < additive_pose->group_count; g++)
        {
            if (additive_pose->groups[g].type_hash == grp_base->type_hash)
            { grp_add = &additive_pose->groups[g]; break; }
        }
        if (!grp_add) continue;

        for (u32 g = 0; g < reference_pose->group_count; g++)
        {
            if (reference_pose->groups[g].type_hash == grp_base->type_hash)
            { grp_ref = &reference_pose->groups[g]; break; }
        }
        if (!grp_ref) continue;

        Mel_Track_Type_Def* type_def = mel_anim_registry_get(grp_base->type_hash);
        u32 stride = type_def->stride;

        if (!type_def->additive_fn) continue;

        for (u32 ib = 0; ib < grp_base->count; ib++)
        {
            u64 prop = grp_base->property_ids[ib];

            if (mask_hashes)
            {
                bool found = false;
                for (u32 m = 0; m < mask_count; m++)
                {
                    if (mask_hashes[m] == prop) { found = true; break; }
                }
                if (!found) continue;
            }

            i32 idx_add = -1, idx_ref = -1;
            for (u32 i = 0; i < grp_add->count; i++)
            {
                if (grp_add->property_ids[i] == prop) { idx_add = (i32)i; break; }
            }
            if (idx_add < 0) continue;

            for (u32 i = 0; i < grp_ref->count; i++)
            {
                if (grp_ref->property_ids[i] == prop) { idx_ref = (i32)i; break; }
            }
            if (idx_ref < 0) continue;

            u8* dst = (u8*)grp_base->data + (usize)ib * stride;
            const u8* add = (const u8*)grp_add->data + (usize)idx_add * stride;
            const u8* ref = (const u8*)grp_ref->data + (usize)idx_ref * stride;

            type_def->additive_fn(dst, add, ref, dst, &weight, 1);
        }
    }
}
