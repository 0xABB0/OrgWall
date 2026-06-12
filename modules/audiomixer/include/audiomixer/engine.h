#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <audiomixer/status.h>
#include <pcm/resample.h>
#include <audioout/audioout.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Future   Mel_Future;
typedef struct Mel_Event    Mel_Event;

typedef struct Mel_Mixer Mel_Mixer;

typedef Mel_Pcm_Resampler Mel_Mixer_Resampler;

typedef struct
{
    u32                 samplerate;
    u32                 channels;
    u32                 block_frames;
    u32                 ring_blocks;
    f32                 master_volume;
    Mel_Mixer_Resampler resampler;
    Mel_Executor*       exec;
    u32                 max_voice_channels;
    f64                 max_voice_ratio;
    Mel_AudioOut        device;
} Mel_Mixer_Opt;

typedef struct
{
    u32 samplerate;
    u32 channels;
    u32 block_frames;
    u32 ring_blocks;
    u32 latency_frames;
} Mel_Mixer_Caps;

Mel_Mixer*     mel_mixer_create(const Mel_Alloc* a, Mel_Mixer_Opt opt);
Mel_Mixer*     mel_mixer_create_offline(const Mel_Alloc* a, Mel_Mixer_Opt opt);
u32            mel_mixer_render(Mel_Mixer* eng, f32* interleaved_dst, u32 frames);
void           mel_mixer_destroy(Mel_Mixer* eng);
Mel_Mixer_Caps mel_mixer_caps(const Mel_Mixer* eng);
void           mel_mixer_set_master_volume(Mel_Mixer* eng, f32 v);
u32            mel_mixer_active_voice_count(const Mel_Mixer* eng);

Mel_Mixer_Status mel_mixer_set_device(Mel_Mixer* eng, Mel_AudioOut device);
Mel_AudioOut     mel_mixer_device(const Mel_Mixer* eng);
Mel_Mixer_Status mel_mixer_device_status(const Mel_Mixer* eng);

#ifdef __cplusplus
}
#endif
