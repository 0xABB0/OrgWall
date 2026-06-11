#pragma once

#include <audioout/audioout.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    str8                     stable_id;
    str8                     name;
    const mel_audioout_kind* kind;
    u32                      channels;
    u32                      samplerate;
    const u32*               samplerates;
    u32                      samplerate_count;
    Mel_AudioOut_Caps        caps;
} Mel_AudioOut_Raw;

typedef struct
{
    u32 samplerate;
    u32 channels;
    u32 block_frames;
} Mel_AudioOut_Format;

typedef u32 (*Mel_AudioOut_Pull_Fn)(void* token, f32* interleaved_dst, u32 frames);

typedef struct
{
    const char* name;
    void*       user;

    u32 (*enumerate)(void* user, Mel_AudioOut_Raw* out, u32 cap);
    str8 (*default_id)(void* user);

    Mel_AudioOut_Status (*open)(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Format* granted, Mel_AudioOut_Pull_Fn pull, void* token);
    void (*start)(void* user, str8 stable_id, void* token);
    void (*stop)(void* user, str8 stable_id, void* token);
    void (*close)(void* user, str8 stable_id, void* token);

    f32 (*volume)(void* user, str8 stable_id);
    Mel_AudioOut_Status (*set_volume)(void* user, str8 stable_id, f32 volume);
    bool (*muted)(void* user, str8 stable_id);
    Mel_AudioOut_Status (*set_muted)(void* user, str8 stable_id, bool muted);

    void* (*native)(void* user, str8 stable_id);
    void (*shutdown)(void* user, const Mel_Alloc* alloc);
} Mel_AudioOut_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_AudioOut_Provider;

Mel_AudioOut_Provider mel_audioout_provider_register(const Mel_AudioOut_Provider_Desc* desc);
void                  mel_audioout_provider_unregister(Mel_AudioOut_Provider p);
void                  mel_audioout_provider_notify(Mel_AudioOut_Provider p);

void mel_audioout__register_host_providers(void);

#ifdef __cplusplus
}
#endif
