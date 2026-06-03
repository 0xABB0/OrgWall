#include "audio_internal.h"

#include <core/types.h>

u32 mel_audio_resample_linear(const f32* src, u32 src_frames, f32* dst, u32 dst_frames, f64 ratio, f64* cursor)
{
    assert(src != NULL);
    assert(dst != NULL);
    assert(cursor != NULL);

    if (dst_frames == 0u)
        return 0u;

    if (src_frames == 0u)
    {
        for (u32 i = 0; i < dst_frames; i++)
            dst[i] = 0.0f;
        return dst_frames;
    }

    f64 pos = *cursor;
    u32 produced = 0;

    for (; produced < dst_frames; produced++)
    {
        f64 fpos = pos;
        if (fpos < 0.0)
            fpos = 0.0;

        u32 i0 = (u32)fpos;
        if (i0 >= src_frames - 1u)
        {
            dst[produced] = src[src_frames - 1u];
        }
        else
        {
            f64 frac = fpos - (f64)i0;
            f32 a = src[i0];
            f32 b = src[i0 + 1u];
            dst[produced] = (f32)((f64)a + ((f64)b - (f64)a) * frac);
        }
        pos += ratio;
    }

    *cursor = pos;
    return produced;
}
