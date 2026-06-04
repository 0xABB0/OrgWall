#pragma once

#include <locale/locale.h>
#include <collection/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_LOCALE_FIELD_PRIMARY = 1u << 0,
    MEL_LOCALE_FIELD_ORDER = 1u << 1,
    MEL_LOCALE_FIELD_MEMBERSHIP = 1u << 2,
};

typedef struct
{
    u32 changed_fields;
} Mel_Locale_Event;

typedef struct Mel_Executor Mel_Executor;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Locale_Subscription;

#define MEL_LOCALE_SUBSCRIPTION_NULL ((Mel_Locale_Subscription){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_Locale_Event_Callback)(const Mel_Locale_Event* ev, void* user);

u32 mel_locale_poll_events(Mel_Locale_Event* out, u32 cap);

Mel_Locale_Subscription mel_locale_subscribe(Mel_Executor* exec, Mel_Locale_Event_Callback cb, void* user);
void                    mel_locale_unsubscribe(Mel_Locale_Subscription sub);

#ifdef __cplusplus
}
#endif
