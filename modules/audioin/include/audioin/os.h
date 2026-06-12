#pragma once

#include <audioin/audioin.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_AudioIn_Published;

#define MEL_AUDIOIN_PUBLISHED_NULL ((Mel_AudioIn_Published){ MEL_SLOTMAP_HANDLE_NULL })

typedef struct
{
    str8 name;
    u32  channels;
    u32  samplerate;
    u32  ring_capacity_frames;
} Mel_AudioIn_Publish_Opt;

typedef struct
{
    Mel_AudioIn_Published published;
    Mel_AudioIn           device;
    Mel_AudioIn_Status    status;
} Mel_AudioIn_Publish_Result;

MEL_NODISCARD Mel_AudioIn_Publish_Result mel_audioin_publish(const Mel_Alloc* a, Mel_AudioIn_Publish_Opt opt);

u32  mel_audioin_publish_feed(Mel_AudioIn_Published p, const f32* interleaved, u32 frames);
bool mel_audioin_publish_os_visible(Mel_AudioIn_Published p);
void mel_audioin_unpublish(Mel_AudioIn_Published p);

void* mel_audioin_native(Mel_AudioIn d);

#ifdef __cplusplus
}
#endif
