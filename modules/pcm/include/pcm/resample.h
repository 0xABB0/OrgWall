#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 (*Mel_Pcm_Resampler)(const f32* src, u32 src_frames, f32* dst, u32 dst_frames, f64 ratio, f64* cursor);

u32 mel_pcm_resample_linear(const f32* src, u32 src_frames, f32* dst, u32 dst_frames, f64 ratio, f64* cursor);

#ifdef __cplusplus
}
#endif
