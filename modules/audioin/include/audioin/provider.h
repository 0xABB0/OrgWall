#pragma once

#include <audioin/audioin.h>
#include <audioin/permission.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    str8                    stable_id;
    str8                    name;
    const mel_audioin_kind* kind;
    u32                     channels;
    u32                     samplerate;
    const u32*              samplerates;
    u32                     samplerate_count;
    Mel_AudioIn_Caps        caps;
} Mel_AudioIn_Raw;

typedef bool (*Mel_AudioIn_Enum_Fn)(const Mel_AudioIn_Raw* raw, void* user);

typedef struct
{
    void (*on_frames)(void* token, const f32* interleaved, u32 frames, u32 samplerate, u32 channels);
    void (*on_lost)(void* token);
    void (*on_auth)(void* token, const mel_audioin_auth* auth);
    void* token;
} Mel_AudioIn_Sink;

typedef struct
{
    const char* name;
    void*       user;

    void (*enumerate)(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user);
    str8 (*default_id)(void* user);

    Mel_AudioIn_Status (*open)(void* user, str8 stable_id, Mel_AudioIn_Sink sink);
    void (*close)(void* user, str8 stable_id, void* token);

    f32 (*gain)(void* user, str8 stable_id);
    Mel_AudioIn_Status (*set_gain)(void* user, str8 stable_id, f32 gain);

    const mel_audioin_auth* (*authorization)(void* user);
    void (*authorize)(void* user, Mel_AudioIn_Sink sink);

    void* (*native)(void* user, str8 stable_id);
    void (*shutdown)(void* user, const Mel_Alloc* alloc);
} Mel_AudioIn_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_AudioIn_Provider;

Mel_AudioIn_Provider mel_audioin_provider_register(const Mel_AudioIn_Provider_Desc* desc);
void                 mel_audioin_provider_unregister(Mel_AudioIn_Provider p);
void                 mel_audioin_provider_notify(Mel_AudioIn_Provider p);

void mel_audioin__register_host_providers(void);

#ifdef __cplusplus
}
#endif
