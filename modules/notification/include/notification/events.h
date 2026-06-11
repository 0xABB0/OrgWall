#pragma once

#include <notification/notification.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_NOTIF_EVENT_PRESENTED = 1u << 0,
    MEL_NOTIF_EVENT_ACTIVATED = 1u << 1,
    MEL_NOTIF_EVENT_ACTION = 1u << 2,
    MEL_NOTIF_EVENT_REPLIED = 1u << 3,
    MEL_NOTIF_EVENT_DISMISSED = 1u << 4,
    MEL_NOTIF_EVENT_AUTH_CHANGED = 1u << 5,
    MEL_NOTIF_EVENT_PUSH_TOKEN = 1u << 6,
    MEL_NOTIF_EVENT_PUSH_RECEIVED = 1u << 7,
};

typedef u32 Mel_Notif_Event_Kind;

typedef struct
{
    Mel_Notif_Event_Kind  kind;
    Mel_Notif             notif;
    str8                  action_id;
    str8                  reply;
    str8                  payload;
    const mel_notif_auth* auth;
} Mel_Notif_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Notif_Subscription;

#define MEL_NOTIF_SUBSCRIPTION_NULL ((Mel_Notif_Subscription){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_Notif_Event_Callback)(const Mel_Notif_Event* ev, void* user);

u32 mel_notif_poll_events(Mel_Notif_Event* out, u32 cap);

Mel_Notif_Subscription mel_notif_subscribe(Mel_Executor* exec, Mel_Notif_Event_Callback cb, void* user);
void                   mel_notif_unsubscribe(Mel_Notif_Subscription sub);

#ifdef __cplusplus
}
#endif
