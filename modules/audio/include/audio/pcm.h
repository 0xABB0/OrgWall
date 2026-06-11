#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <audio/source.h>
#include <audio/ownership.h>

#ifdef __cplusplus
extern "C"
{
#endif

Mel_Audio_Source* mel_audio_pcm_from_float(const Mel_Alloc* a, const f32* interleaved,
                                           u32 frames, u32 channels, u32 samplerate,
                                           Mel_Audio_Ownership own);
void mel_audio_pcm_set_loop(Mel_Audio_Source* s, bool loop, f64 loop_start_seconds);

typedef u32 (*Mel_Audio_Pull_Fn)(void* user, f32* interleaved_dst, u32 frames);

Mel_Audio_Source* mel_audio_pull_source(const Mel_Alloc* a, Mel_Audio_Pull_Fn fn, void* user,
                                        u32 channels, u32 samplerate);

#ifdef __cplusplus
}
#endif
