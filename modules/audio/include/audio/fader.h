#pragma once

#include <core/types.h>
#include <audio/engine.h>
#include <audio/voice.h>

#ifdef __cplusplus
extern "C"
{
#endif

void mel_audio_fade_volume(Mel_Audio* eng, Mel_Audio_Voice v, f32 to, f64 seconds);
void mel_audio_fade_pan(Mel_Audio* eng, Mel_Audio_Voice v, f32 to, f64 seconds);
void mel_audio_fade_play_speed(Mel_Audio* eng, Mel_Audio_Voice v, f64 to, f64 seconds);
void mel_audio_oscillate_volume(Mel_Audio* eng, Mel_Audio_Voice v, f32 lo, f32 hi, f64 period);
void mel_audio_schedule_pause(Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds);
void mel_audio_schedule_stop(Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds);
void mel_audio_fade_master_volume(Mel_Audio* eng, f32 to, f64 seconds);

#ifdef __cplusplus
}
#endif
