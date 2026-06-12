#include "mixer_internal.h"

#include <core/types.h>
#include <math/scalar.h>

void mel_mixer__pan_gains(f32 pan, f32* out_l, f32* out_r)
{
    assert(out_l != NULL);
    assert(out_r != NULL);

    f32 p = mel_clampf(pan, -1.0f, 1.0f);
    f32 theta = (p + 1.0f) * 0.5f * MEL_HALF_PI;
    *out_l = mel_cosf(theta);
    *out_r = mel_sinf(theta);
}

void mel_mixer__pan_accumulate(const f32* voice_planar, u32 src_channels, u32 frames, f32 gain_l, f32 gain_r, f32* planar_out, u32 out_channels)
{
    assert(voice_planar != NULL);
    assert(planar_out != NULL);
    assert(src_channels >= 1u);
    assert(out_channels >= 1u);

    const f32* src_l = voice_planar;
    const f32* src_r = src_channels >= 2u ? voice_planar + frames : voice_planar;

    if (out_channels >= 2u)
    {
        f32* out_l = planar_out;
        f32* out_r = planar_out + frames;
        for (u32 i = 0; i < frames; i++)
        {
            out_l[i] += src_l[i] * gain_l;
            out_r[i] += src_r[i] * gain_r;
        }
        for (u32 c = 2; c < out_channels; c++)
        {
            const f32* sc = src_channels > c ? voice_planar + (usize)c * frames : src_l;
            f32*       oc = planar_out + (usize)c * frames;
            for (u32 i = 0; i < frames; i++)
                oc[i] += sc[i] * gain_l;
        }
    }
    else
    {
        MEL_UNUSED(gain_l);
        MEL_UNUSED(gain_r);
        f32* out = planar_out;
        if (src_channels >= 2u)
        {
            for (u32 i = 0; i < frames; i++)
                out[i] += (src_l[i] + src_r[i]) * 0.5f;
        }
        else
        {
            for (u32 i = 0; i < frames; i++)
                out[i] += src_l[i];
        }
    }
}
