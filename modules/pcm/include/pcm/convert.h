#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

void mel_pcm_interleave(f32* interleaved_dst, const f32* const* planar_src, u32 channels, u32 frames);
void mel_pcm_deinterleave(f32* const* planar_dst, const f32* interleaved_src, u32 channels, u32 frames);

void mel_pcm_i16_to_f32(f32* dst, const i16* src, u32 samples);
void mel_pcm_f32_to_i16(i16* dst, const f32* src, u32 samples);

#ifdef __cplusplus
}
#endif
