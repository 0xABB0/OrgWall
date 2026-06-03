#pragma once

#include <core/types.h>
#include <collection.slotmap/slotmap.fwd.h>
#include <audio/engine.h>
#include <audio/source.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_SlotMap_Handle slot;
} Mel_Audio_Voice;

#define MEL_AUDIO_VOICE_PAUSED                (1u << 0)
#define MEL_AUDIO_VOICE_LOOPING               (1u << 1)
#define MEL_AUDIO_VOICE_PROTECTED             (1u << 2)
#define MEL_AUDIO_VOICE_INAUDIBLE_KEEP_TICKING (1u << 3)

Mel_Audio_Voice mel_audio_play(Mel_Audio* eng, Mel_Audio_Source* src);
Mel_Audio_Voice mel_audio_play_ex(Mel_Audio* eng, Mel_Audio_Source* src,
                                  f32 volume, f32 pan, bool start_paused);

bool mel_audio_voice_valid(const Mel_Audio* eng, Mel_Audio_Voice v);
void mel_audio_set_volume(Mel_Audio* eng, Mel_Audio_Voice v, f32 volume);
void mel_audio_set_pan(Mel_Audio* eng, Mel_Audio_Voice v, f32 pan);
void mel_audio_set_play_speed(Mel_Audio* eng, Mel_Audio_Voice v, f64 ratio);
void mel_audio_set_paused(Mel_Audio* eng, Mel_Audio_Voice v, bool paused);
void mel_audio_set_looping(Mel_Audio* eng, Mel_Audio_Voice v, bool loop);
void mel_audio_seek(Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds);
void mel_audio_stop(Mel_Audio* eng, Mel_Audio_Voice v);
void mel_audio_stop_all(Mel_Audio* eng);

#ifdef __cplusplus
}
#endif
