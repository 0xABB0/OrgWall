#pragma once

#include <core/types.h>
#include <audio/engine.h>
#include <audio/voice.h>

#ifdef __cplusplus
extern "C"
{
#endif

Mel_Future* mel_audio_voice_end_future(Mel_Audio* eng, Mel_Audio_Voice v);
Mel_Event* mel_audio_device_events(Mel_Audio* eng);

#ifdef __cplusplus
}
#endif
