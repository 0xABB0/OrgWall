#include <pcm/convert.h>

#include <core/types.h>

void mel_pcm_interleave(f32* interleaved_dst, const f32* const* planar_src, u32 channels, u32 frames)
{
    assert(interleaved_dst != NULL);
    assert(planar_src != NULL);

    for (u32 c = 0; c < channels; c++)
    {
        const f32* plane = planar_src[c];
        assert(plane != NULL);
        assert(plane != interleaved_dst);

        f32* dst = interleaved_dst + c;
        for (u32 i = 0; i < frames; i++)
            dst[(usize)i * channels] = plane[i];
    }
}

void mel_pcm_deinterleave(f32* const* planar_dst, const f32* interleaved_src, u32 channels, u32 frames)
{
    assert(planar_dst != NULL);
    assert(interleaved_src != NULL);

    for (u32 c = 0; c < channels; c++)
    {
        f32* plane = planar_dst[c];
        assert(plane != NULL);
        assert(plane != interleaved_src);

        const f32* src = interleaved_src + c;
        for (u32 i = 0; i < frames; i++)
            plane[i] = src[(usize)i * channels];
    }
}

void mel_pcm_i16_to_f32(f32* dst, const i16* src, u32 samples)
{
    assert(dst != NULL);
    assert(src != NULL);

    const f32 scale = 1.0f / 32768.0f;
    for (u32 i = 0; i < samples; i++)
        dst[i] = (f32)src[i] * scale;
}

void mel_pcm_f32_to_i16(i16* dst, const f32* src, u32 samples)
{
    assert(dst != NULL);
    assert(src != NULL);

    for (u32 i = 0; i < samples; i++)
    {
        f32 x = src[i] * 32768.0f;
        if (x >= 32767.0f)
            dst[i] = 32767;
        else if (x <= -32768.0f)
            dst[i] = -32768;
        else
            dst[i] = (i16)(x >= 0.0f ? x + 0.5f : x - 0.5f);
    }
}
