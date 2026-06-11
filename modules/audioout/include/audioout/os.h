#pragma once

#include <audioout/audioout.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_AudioOut_Published;

#define MEL_AUDIOOUT_PUBLISHED_NULL ((Mel_AudioOut_Published){ MEL_SLOTMAP_HANDLE_NULL })

typedef struct
{
    str8 name;
    u32  channels;
    u32  samplerate;
    u32  ring_capacity_frames;
} Mel_AudioOut_Publish_Opt;

typedef struct
{
    Mel_AudioOut_Published published;
    Mel_AudioOut           device;
    Mel_AudioOut_Status    status;
} Mel_AudioOut_Publish_Result;

MEL_NODISCARD Mel_AudioOut_Publish_Result mel_audioout_publish(const Mel_Alloc* a, Mel_AudioOut_Publish_Opt opt);

u32  mel_audioout_publish_read(Mel_AudioOut_Published p, f32* interleaved_dst, u32 max_frames);
bool mel_audioout_publish_os_visible(Mel_AudioOut_Published p);
void mel_audioout_unpublish(Mel_AudioOut_Published p);

void* mel_audioout_native(Mel_AudioOut d);

#ifdef __cplusplus
}
#endif
