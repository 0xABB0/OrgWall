#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <audio/status.h>
#include <pcm/resample.h>
#include <audioout/audioout.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Future   Mel_Future;
typedef struct Mel_Event    Mel_Event;

typedef struct Mel_Audio Mel_Audio;

typedef Mel_Pcm_Resampler Mel_Audio_Resampler;

typedef struct
{
    u32                 samplerate;
    u32                 channels;
    u32                 block_frames;
    u32                 ring_blocks;
    f32                 master_volume;
    Mel_Audio_Resampler resampler;
    Mel_Executor*       exec;
    u32                 max_voice_channels;
    f64                 max_voice_ratio;
    Mel_AudioOut        device;
} Mel_Audio_Opt;

typedef struct
{
    u32 samplerate;
    u32 channels;
    u32 block_frames;
    u32 ring_blocks;
    u32 latency_frames;
} Mel_Audio_Caps;

Mel_Audio*     mel_audio_create(const Mel_Alloc* a, Mel_Audio_Opt opt);
Mel_Audio*     mel_audio_create_offline(const Mel_Alloc* a, Mel_Audio_Opt opt);
u32            mel_audio_render(Mel_Audio* eng, f32* interleaved_dst, u32 frames);
void           mel_audio_destroy(Mel_Audio* eng);
Mel_Audio_Caps mel_audio_caps(const Mel_Audio* eng);
void           mel_audio_set_master_volume(Mel_Audio* eng, f32 v);
u32            mel_audio_active_voice_count(const Mel_Audio* eng);

Mel_Audio_Status mel_audio_set_device(Mel_Audio* eng, Mel_AudioOut device);
Mel_AudioOut     mel_audio_device(const Mel_Audio* eng);
Mel_Audio_Status mel_audio_device_status(const Mel_Audio* eng);

#ifdef __cplusplus
}
#endif
