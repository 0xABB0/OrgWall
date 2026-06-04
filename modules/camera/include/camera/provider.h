#pragma once

#include <camera/camera.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64                      stable_id;
    str8                     name;
    const mel_camera_facing* facing;
    const Mel_Camera_Mode*   modes;
    u32                      mode_count;
} Mel_Camera_Raw;

typedef struct
{
    void (*on_frame)(void* token, const Mel_Camera_Frame* frame);
    void (*on_event)(void* token, Mel_Camera_Event ev);
    void (*on_auth)(void* token, const mel_camera_auth* auth);
    void* token;
} Mel_Camera_Sink;

typedef struct
{
    const char* name;
    void*       user;

    u32  (*enumerate)(void* user, Mel_Camera_Raw* out, u32 cap);
    bool (*open)(void* user, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink);
    void (*close)(void* user, u64 stable_id);
    Mel_Camera_Status (*start)(void* user, u64 stable_id);
    Mel_Camera_Status (*stop)(void* user, u64 stable_id);

    const mel_camera_auth* (*authorization)(void* user);
    void (*authorize)(void* user, Mel_Camera_Sink sink);

    void* (*native)(void* user, u64 stable_id);
} Mel_Camera_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Camera_Provider;

Mel_Camera_Provider mel_camera_provider_register(const Mel_Camera_Provider_Desc* desc);
void                mel_camera_provider_unregister(Mel_Camera_Provider p);

void mel_camera__register_host_providers(void);

#ifdef __cplusplus
}
#endif
