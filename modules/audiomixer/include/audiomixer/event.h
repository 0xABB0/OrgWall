#pragma once

#include <core/types.h>
#include <audiomixer/engine.h>
#include <audiomixer/voice.h>

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
} Mel_Mixer_Device_Event;

Mel_Future* mel_mixer_voice_end_future(Mel_Mixer* eng, Mel_Mixer_Voice v);
void        mel_mixer_voice_end_future_free(Mel_Mixer* eng, Mel_Future* fut);
Mel_Event*  mel_mixer_device_events(Mel_Mixer* eng);

#ifdef __cplusplus
}
#endif
