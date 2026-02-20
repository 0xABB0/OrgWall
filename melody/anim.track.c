#include "anim.track.h"
#include "math.curve.h"
#include "math.scalar.h"
#include "allocator.h"

#include <string.h>
#include <math.h>

void mel_anim_track_init(Mel_Anim_Track* track, const Mel_Alloc* alloc,
                         u64 property_id, u32 stride, u32 keyframe_count)
{
    assert(track != NULL);
    assert(alloc != NULL);
    assert(stride > 0);
    assert(keyframe_count > 0);

    track->property_id = property_id;
    track->stride = stride;
    track->keyframe_count = keyframe_count;

    track->times = mel_alloc_array(alloc, f32, keyframe_count);
    track->values = mel_alloc_array(alloc, f32, keyframe_count * stride);
    track->curve_types = mel_alloc_array(alloc, u32, keyframe_count);
    track->beziers = NULL;
    track->blend = NULL;
}

void mel_anim_track_destroy(Mel_Anim_Track* track, const Mel_Alloc* alloc)
{
    assert(track != NULL);
    assert(alloc != NULL);

    mel_dealloc(alloc, track->times);
    mel_dealloc(alloc, track->values);
    mel_dealloc(alloc, track->curve_types);
    if (track->beziers)
        mel_dealloc(alloc, track->beziers);

    *track = (Mel_Anim_Track){0};
}

void mel_anim_track_set_keyframe(Mel_Anim_Track* track, u32 index,
                                 f32 time, const f32* value, u32 curve_type)
{
    assert(track != NULL);
    assert(index < track->keyframe_count);
    assert(value != NULL);

    track->times[index] = time;
    memcpy(&track->values[index * track->stride], value, sizeof(f32) * track->stride);
    track->curve_types[index] = curve_type;
}

void mel_anim_track_set_bezier(Mel_Anim_Track* track, const Mel_Alloc* alloc,
                               u32 index, f32 cx1, f32 cy1, f32 cx2, f32 cy2)
{
    assert(track != NULL);
    assert(alloc != NULL);
    assert(index < track->keyframe_count);

    if (!track->beziers)
        track->beziers = mel_alloc_array(alloc, Mel_Bezier, track->keyframe_count);

    track->curve_types[index] = MEL_CURVE_BEZIER;
    mel_bezier_init(&track->beziers[index], cx1, cy1, cx2, cy2);
}

u32 mel_anim_track_find_keyframe(const Mel_Anim_Track* track, f32 time)
{
    assert(track != NULL);
    assert(track->keyframe_count > 0);

    u32 lo = 0, hi = track->keyframe_count;
    while (lo < hi)
    {
        u32 mid = (lo + hi) / 2;
        if (track->times[mid] <= time) lo = mid + 1;
        else hi = mid;
    }

    return lo > 0 ? lo - 1 : 0;
}

void mel_anim_track_eval(const Mel_Anim_Track* track, f32 time, f32* out)
{
    assert(track != NULL);
    assert(out != NULL);
    assert(track->keyframe_count > 0);

    if (track->keyframe_count == 1 || time <= track->times[0])
    {
        memcpy(out, &track->values[0], sizeof(f32) * track->stride);
        return;
    }

    u32 last = track->keyframe_count - 1;
    if (time >= track->times[last])
    {
        memcpy(out, &track->values[last * track->stride], sizeof(f32) * track->stride);
        return;
    }

    u32 left = mel_anim_track_find_keyframe(track, time);
    u32 right = left + 1;
    assert(right < track->keyframe_count);

    f32 t_left = track->times[left];
    f32 t_right = track->times[right];
    f32 span = t_right - t_left;
    f32 t = (span > 1e-7f) ? (time - t_left) / span : 0.0f;

    u32 curve = track->curve_types[left];
    const Mel_Bezier* bez = (curve == MEL_CURVE_BEZIER && track->beziers)
                                 ? &track->beziers[left] : NULL;
    f32 alpha = mel_curve_eval(curve, t, bez);

    const f32* v0 = &track->values[left * track->stride];
    const f32* v1 = &track->values[right * track->stride];

    if (track->blend)
    {
        track->blend(out, v0, v1, track->stride, alpha);
    }
    else
    {
        for (u32 i = 0; i < track->stride; i++)
            out[i] = v0[i] + (v1[i] - v0[i]) * alpha;
    }
}

void mel_blend_angle(f32* out, const f32* a, const f32* b, u32 stride, f32 t)
{
    for (u32 i = 0; i < stride; i++)
        out[i] = mel_angle_lerp(a[i], b[i], t);
}

void mel_blend_quat_slerp(f32* out, const f32* a, const f32* b, u32 stride, f32 t)
{
    assert(stride == 4);
    (void)stride;

    f32 dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];

    f32 bx = b[0], by = b[1], bz = b[2], bw = b[3];
    if (dot < 0.0f)
    {
        bx = -bx; by = -by; bz = -bz; bw = -bw;
        dot = -dot;
    }

    if (dot > 0.9995f)
    {
        out[0] = a[0] + (bx - a[0]) * t;
        out[1] = a[1] + (by - a[1]) * t;
        out[2] = a[2] + (bz - a[2]) * t;
        out[3] = a[3] + (bw - a[3]) * t;

        f32 len = mel_sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
        f32 inv = 1.0f / len;
        out[0] *= inv; out[1] *= inv; out[2] *= inv; out[3] *= inv;
        return;
    }

    f32 theta0 = mel_acosf(dot);
    f32 theta  = theta0 * t;
    f32 sin_theta  = mel_sinf(theta);
    f32 sin_theta0 = mel_sinf(theta0);

    f32 s0 = mel_cosf(theta) - dot * sin_theta / sin_theta0;
    f32 s1 = sin_theta / sin_theta0;

    out[0] = a[0] * s0 + bx * s1;
    out[1] = a[1] * s0 + by * s1;
    out[2] = a[2] * s0 + bz * s1;
    out[3] = a[3] * s0 + bw * s1;
}
