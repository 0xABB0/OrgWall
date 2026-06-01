#pragma once

#include <vibration/vibration.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64          stable_id;
    str8         name;
    Mel_Vib_Caps caps;
} Mel_Vib_Raw;

typedef struct
{
    const Mel_Vib_Event* events;
    u32                  count;
    f32                  total_duration_s;
    u32                  loop;
    Mel_Vib_Caps         caps;
} Mel_Vib_Lowered;

typedef struct
{
    void (*notify)(void* token, Mel_Vib_Status status);
    void* token;
} Mel_Vib_Completion;

typedef struct
{
    const char* name;
    void*       user;

    u32  (*enumerate)(void* user, Mel_Vib_Raw* out, u32 cap);
    bool (*open)(void* user, u64 stable_id, Mel_Vib_Descriptor* out);
    void (*close)(void* user, u64 stable_id);

    Mel_Vib_Status (*submit)(void* user, u64 stable_id, u64 playback_token, const Mel_Vib_Lowered* lowered, Mel_Vib_Completion completion);
    void (*abort)(void* user, u64 stable_id, u64 playback_token);
    void (*pause)(void* user, u64 stable_id, u64 playback_token);
    void (*resume)(void* user, u64 stable_id, u64 playback_token);

    void* (*native)(void* user, u64 stable_id);
} Mel_Vib_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Vib_Provider;

Mel_Vib_Provider mel_vib_provider_register(const Mel_Vib_Provider_Desc* desc);
void             mel_vib_provider_unregister(Mel_Vib_Provider p);

void mel_vib__register_host_providers(void);

#ifdef __cplusplus
}
#endif
