#pragma once

#include <gamepad/joystick.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_JOYSTICK_FIELD_POWER        = 1u << 0,
    MEL_JOYSTICK_FIELD_PLAYER_INDEX = 1u << 1,
    MEL_JOYSTICK_FIELD_FEATURES     = 1u << 2,
    MEL_JOYSTICK_FIELD_NAME         = 1u << 3,
};

typedef enum
{
    MEL_JOYSTICK_EVENT_ADDED = 0,
    MEL_JOYSTICK_EVENT_REMOVED,
    MEL_JOYSTICK_EVENT_CHANGED,
} Mel_Joystick_Event_Kind;

typedef struct
{
    Mel_Joystick_Event_Kind kind;
    Mel_Joystick            joystick;
    u32                     changed_fields;
} Mel_Joystick_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Joystick_Subscription;

#define MEL_JOYSTICK_SUBSCRIPTION_NULL ((Mel_Joystick_Subscription){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_Joystick_Event_Callback)(const Mel_Joystick_Event* ev, void* user);

u32 mel_joystick_poll_events(Mel_Joystick_Event* out, u32 cap);

Mel_Joystick_Subscription mel_joystick_subscribe(Mel_Executor* exec, Mel_Joystick_Event_Callback cb, void* user);
void                      mel_joystick_unsubscribe(Mel_Joystick_Subscription sub);

#ifdef __cplusplus
}
#endif
