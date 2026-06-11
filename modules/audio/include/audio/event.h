#pragma once

#include <core/types.h>
#include <audio/engine.h>
#include <audio/voice.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_AudioOut device;
    bool         lost;
    bool         format_changed;
    bool         default_changed;
    bool         interrupted;
    bool         resumed;
} Mel_Audio_Device_Event;

Mel_Future* mel_audio_voice_end_future(Mel_Audio* eng, Mel_Audio_Voice v);
void        mel_audio_voice_end_future_free(Mel_Audio* eng, Mel_Future* fut);
Mel_Event*  mel_audio_device_events(Mel_Audio* eng);

#ifdef __cplusplus
}
#endif
