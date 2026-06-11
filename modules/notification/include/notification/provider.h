#pragma once

#include <notification/notification.h>
#include <notification/events.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    void (*on_auth)(void* token, const mel_notif_auth* auth);
    void* token;
} Mel_Notif_Sink;

typedef struct
{
    u64                      token;
    const Mel_Notif_Content* content;
    Mel_Notif_Trigger        trigger;
    bool                     scheduled;
} Mel_Notif_Lowered;

typedef struct
{
    const char* name;
    void*       user;

    bool (*supported)(void* user);
    Mel_Notif_Caps (*caps)(void* user);

    const mel_notif_auth* (*authorization)(void* user);
    void (*authorize)(void* user, Mel_Notif_Sink sink);

    Mel_Notif_Status (*channel_register)(void* user, const Mel_Notif_Channel_Opt* opt);

    Mel_Notif_Status (*post)(void* user, const Mel_Notif_Lowered* lowered);
    Mel_Notif_Status (*update)(void* user, const Mel_Notif_Lowered* lowered);
    void (*cancel)(void* user, u64 token);
    void (*cancel_all)(void* user);

    Mel_Notif_Status (*push_register)(void* user);
    Mel_Notif_Status (*push_unregister)(void* user);

    void (*shutdown)(void* user);
} Mel_Notif_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Notif_Provider;

Mel_Notif_Provider mel_notif_provider_register(const Mel_Notif_Provider_Desc* desc);
void               mel_notif_provider_unregister(Mel_Notif_Provider p);

void mel_notif__register_host_providers(void);
void mel_notif__force_provider(Mel_Notif_Provider p);

void mel_notif__dispatch_presented(u64 token);
void mel_notif__dispatch_activated(u64 token, str8 action_id, str8 reply, str8 payload);
void mel_notif__dispatch_dismissed(u64 token);
void mel_notif__dispatch_auth_changed(const mel_notif_auth* auth);
void mel_notif__dispatch_push_token(str8 token_bytes);
void mel_notif__dispatch_push(str8 payload);

#ifdef __cplusplus
}
#endif
