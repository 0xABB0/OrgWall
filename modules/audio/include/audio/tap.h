#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <allocator/allocator.fwd.h>
#include <audio/engine.h>
#include <audio/voice.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Audio_Tap Mel_Audio_Tap;

MEL_NODISCARD Mel_Audio_Tap* mel_audio_tap_open(Mel_Audio* eng, const Mel_Alloc* a, u32 ring_frames);
MEL_NODISCARD Mel_Audio_Tap* mel_audio_voice_tap_open(Mel_Audio* eng, Mel_Audio_Voice v, const Mel_Alloc* a, u32 ring_frames);

MEL_NODISCARD u32 mel_audio_tap_read(Mel_Audio_Tap* t, f32* interleaved_dst, u32 max_frames);
MEL_NODISCARD u32 mel_audio_tap_available(const Mel_Audio_Tap* t);

u64 mel_audio_tap_dropped_frames(const Mel_Audio_Tap* t);

void mel_audio_tap_close(Mel_Audio_Tap* t);

#ifdef __cplusplus
}
#endif
