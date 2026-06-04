#pragma once

#include <hid/hid.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_HID_FIELD_IDENTITY = 1u << 0,
    MEL_HID_FIELD_USAGE = 1u << 1,
    MEL_HID_FIELD_REPORTS = 1u << 2,
    MEL_HID_FIELD_STRINGS = 1u << 3,
    MEL_HID_FIELD_BUS = 1u << 4,
};

// Event-kind discriminant kept as an open u32 namespace, not a C enum (MEL-CODE-001): the only new
// enums this module mints are none. A future kind (a re-enumeration hint, a permission grant)
// extends the namespace without a closed-set rewrite.
typedef u32 Mel_Hid_Event_Kind;

#define MEL_HID_EVENT_ADDED   0u
#define MEL_HID_EVENT_REMOVED 1u
#define MEL_HID_EVENT_CHANGED 2u

typedef struct
{
    Mel_Hid_Event_Kind kind;
    Mel_Hid_Device     device;
    u32                changed_fields;
} Mel_Hid_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Hid_Subscription;

#define MEL_HID_SUBSCRIPTION_NULL ((Mel_Hid_Subscription){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_Hid_Event_Callback)(const Mel_Hid_Event* ev, void* user);

u32 mel_hid_poll_events(Mel_Hid_Event* out, u32 cap);

Mel_Hid_Subscription mel_hid_subscribe(Mel_Executor* exec, Mel_Hid_Event_Callback cb, void* user);
void                 mel_hid_unsubscribe(Mel_Hid_Subscription sub);

#ifdef __cplusplus
}
#endif
