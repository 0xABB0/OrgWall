#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <allocator/allocator.fwd.h>
#include <audiomixer/engine.h>
#include <audiomixer/voice.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Mixer_Tap Mel_Mixer_Tap;

MEL_NODISCARD Mel_Mixer_Tap* mel_mixer_tap_open(Mel_Mixer* eng, const Mel_Alloc* a, u32 ring_frames);
MEL_NODISCARD Mel_Mixer_Tap* mel_mixer_voice_tap_open(Mel_Mixer* eng, Mel_Mixer_Voice v, const Mel_Alloc* a, u32 ring_frames);

MEL_NODISCARD u32 mel_mixer_tap_read(Mel_Mixer_Tap* t, f32* interleaved_dst, u32 max_frames);
MEL_NODISCARD u32 mel_mixer_tap_available(const Mel_Mixer_Tap* t);

u64 mel_mixer_tap_dropped_frames(const Mel_Mixer_Tap* t);

void mel_mixer_tap_close(Mel_Mixer_Tap* t);

#ifdef __cplusplus
}
#endif
