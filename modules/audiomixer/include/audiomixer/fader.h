#pragma once

#include <core/types.h>
#include <audiomixer/engine.h>
#include <audiomixer/voice.h>

#ifdef __cplusplus
extern "C"
{
#endif

void mel_mixer_fade_volume(Mel_Mixer* eng, Mel_Mixer_Voice v, f32 to, f64 seconds);
void mel_mixer_fade_pan(Mel_Mixer* eng, Mel_Mixer_Voice v, f32 to, f64 seconds);
void mel_mixer_fade_play_speed(Mel_Mixer* eng, Mel_Mixer_Voice v, f64 to, f64 seconds);
void mel_mixer_oscillate_volume(Mel_Mixer* eng, Mel_Mixer_Voice v, f32 lo, f32 hi, f64 period);
void mel_mixer_schedule_pause(Mel_Mixer* eng, Mel_Mixer_Voice v, f64 seconds);
void mel_mixer_schedule_stop(Mel_Mixer* eng, Mel_Mixer_Voice v, f64 seconds);
void mel_mixer_fade_master_volume(Mel_Mixer* eng, f32 to, f64 seconds);

#ifdef __cplusplus
}
#endif
