#pragma once

#include <tray/tray.h>
#include <tray/menu.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_TRAY_EVENT_ACTIVATED = 1u << 0,
    MEL_TRAY_EVENT_ITEM_CLICKED = 1u << 1,
    MEL_TRAY_EVENT_MENU_OPENED = 1u << 2,
    MEL_TRAY_EVENT_MENU_CLOSED = 1u << 3,
};

typedef u32 Mel_Tray_Event_Kind;

enum
{
    MEL_TRAY_BUTTON_LEFT = 1u << 0,
    MEL_TRAY_BUTTON_RIGHT = 1u << 1,
    MEL_TRAY_BUTTON_MIDDLE = 1u << 2,
};

typedef u32 Mel_Tray_Buttons;

typedef struct
{
    Mel_Tray_Event_Kind kind;
    Mel_Tray            tray;
    Mel_Tray_Item       item;
    Mel_Tray_Buttons    buttons;
} Mel_Tray_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Tray_Subscription;

#define MEL_TRAY_SUBSCRIPTION_NULL ((Mel_Tray_Subscription){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_Tray_Event_Callback)(const Mel_Tray_Event* ev, void* user);

u32 mel_tray_poll_events(Mel_Tray_Event* out, u32 cap);

Mel_Tray_Subscription mel_tray_subscribe(Mel_Executor* exec, Mel_Tray_Event_Callback cb, void* user);
void                  mel_tray_unsubscribe(Mel_Tray_Subscription sub);

#ifdef __cplusplus
}
#endif
