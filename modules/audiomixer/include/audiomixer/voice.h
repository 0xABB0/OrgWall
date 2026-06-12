#pragma once

#include <core/types.h>
#include <collection/slotmap.fwd.h>
#include <audiomixer/engine.h>
#include <audiomixer/source.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_SlotMap_Handle slot;
} Mel_Mixer_Voice;

#define MEL_MIXER_VOICE_PAUSED                (1u << 0)
#define MEL_MIXER_VOICE_LOOPING               (1u << 1)
#define MEL_MIXER_VOICE_PROTECTED             (1u << 2)
#define MEL_MIXER_VOICE_INAUDIBLE_KEEP_TICKING (1u << 3)

Mel_Mixer_Voice mel_mixer_play(Mel_Mixer* eng, Mel_Mixer_Source* src);
Mel_Mixer_Voice mel_mixer_play_ex(Mel_Mixer* eng, Mel_Mixer_Source* src,
                                  f32 volume, f32 pan, bool start_paused);

bool mel_mixer_voice_valid(const Mel_Mixer* eng, Mel_Mixer_Voice v);
void mel_mixer_set_volume(Mel_Mixer* eng, Mel_Mixer_Voice v, f32 volume);
void mel_mixer_set_pan(Mel_Mixer* eng, Mel_Mixer_Voice v, f32 pan);
void mel_mixer_set_play_speed(Mel_Mixer* eng, Mel_Mixer_Voice v, f64 ratio);
void mel_mixer_set_paused(Mel_Mixer* eng, Mel_Mixer_Voice v, bool paused);
void mel_mixer_set_looping(Mel_Mixer* eng, Mel_Mixer_Voice v, bool loop);
void mel_mixer_seek(Mel_Mixer* eng, Mel_Mixer_Voice v, f64 seconds);
void mel_mixer_stop(Mel_Mixer* eng, Mel_Mixer_Voice v);
void mel_mixer_stop_all(Mel_Mixer* eng);

#ifdef __cplusplus
}
#endif
