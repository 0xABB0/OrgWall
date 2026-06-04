#pragma once

#include <input/input.h>
#include <input/scancode.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_INPUT_FIELD_NAME = 1u << 0,
    MEL_INPUT_FIELD_CAPS = 1u << 1,
    MEL_INPUT_FIELD_LAYOUT = 1u << 2,
    MEL_INPUT_FIELD_BUTTONS = 1u << 3,
    MEL_INPUT_FIELD_TOUCH = 1u << 4,
    MEL_INPUT_FIELD_PEN = 1u << 5,
};

typedef enum
{
    MEL_INPUT_DEVICE_EVENT_ADDED = 0,
    MEL_INPUT_DEVICE_EVENT_REMOVED,
    MEL_INPUT_DEVICE_EVENT_CHANGED,
} Mel_Input_Device_Event_Kind;

typedef struct
{
    Mel_Input_Device_Event_Kind kind;
    Mel_Input_Device            device;
    u32                         changed_fields;
} Mel_Input_Device_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Input_Subscription;

#define MEL_INPUT_SUBSCRIPTION_NULL ((Mel_Input_Subscription){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_Input_Device_Event_Callback)(const Mel_Input_Device_Event* ev, void* user);

u32 mel_input_poll_events(Mel_Input_Device_Event* out, u32 cap);

Mel_Input_Subscription mel_input_subscribe(Mel_Executor* exec, Mel_Input_Device_Event_Callback cb, void* user);
void                   mel_input_unsubscribe(Mel_Input_Subscription sub);

enum
{
    MEL_INPUT_MOD_LSHIFT = 1u << 0,
    MEL_INPUT_MOD_RSHIFT = 1u << 1,
    MEL_INPUT_MOD_LCTRL = 1u << 2,
    MEL_INPUT_MOD_RCTRL = 1u << 3,
    MEL_INPUT_MOD_LALT = 1u << 4,
    MEL_INPUT_MOD_RALT = 1u << 5,
    MEL_INPUT_MOD_LGUI = 1u << 6,
    MEL_INPUT_MOD_RGUI = 1u << 7,
    MEL_INPUT_MOD_CAPS = 1u << 8,
    MEL_INPUT_MOD_NUM = 1u << 9,
    MEL_INPUT_MOD_SCROLL = 1u << 10,
    MEL_INPUT_MOD_MODE = 1u << 11,

    MEL_INPUT_MOD_SHIFT = MEL_INPUT_MOD_LSHIFT | MEL_INPUT_MOD_RSHIFT,
    MEL_INPUT_MOD_CTRL = MEL_INPUT_MOD_LCTRL | MEL_INPUT_MOD_RCTRL,
    MEL_INPUT_MOD_ALT = MEL_INPUT_MOD_LALT | MEL_INPUT_MOD_RALT,
    MEL_INPUT_MOD_GUI = MEL_INPUT_MOD_LGUI | MEL_INPUT_MOD_RGUI,
};

typedef struct
{
    Mel_Input_Device device;
    Mel_Scancode     scancode;
    Mel_Keycode      keycode;
    u32              modifiers;
    bool             down;
    bool             repeat;
} Mel_Input_Key_Event;

enum
{
    MEL_INPUT_TEXT_COMMIT = 0,
    MEL_INPUT_TEXT_COMPOSITION,
    MEL_INPUT_TEXT_CANDIDATE,
};

typedef struct
{
    Mel_Input_Device device;
    u32              phase;
    str8             text;
    i32              cursor_begin;
    i32              cursor_end;
    u32              candidate_index;
    u32              candidate_count;
} Mel_Input_Text_Event;

enum
{
    MEL_INPUT_MOUSE_BUTTON_LEFT = 1u << 0,
    MEL_INPUT_MOUSE_BUTTON_RIGHT = 1u << 1,
    MEL_INPUT_MOUSE_BUTTON_MIDDLE = 1u << 2,
    MEL_INPUT_MOUSE_BUTTON_X1 = 1u << 3,
    MEL_INPUT_MOUSE_BUTTON_X2 = 1u << 4,
};

typedef struct
{
    Mel_Input_Device device;
    f32              x, y;
    f32              global_x, global_y;
    f32              dx, dy;
    u32              buttons;
    u32              button_changed;
    bool             button_down;
    f32              wheel_x, wheel_y;
    bool             wheel_flipped;
} Mel_Input_Mouse_Event;

enum
{
    MEL_INPUT_TOUCH_DOWN = 0,
    MEL_INPUT_TOUCH_MOVE,
    MEL_INPUT_TOUCH_UP,
    MEL_INPUT_TOUCH_CANCEL,
};

typedef struct
{
    Mel_Input_Device device;
    u64              finger_id;
    u32              phase;
    f32              x, y;
    f32              dx, dy;
    f32              pressure;
    bool             direct;
} Mel_Input_Touch_Event;

enum
{
    MEL_INPUT_PEN_DOWN = 0,
    MEL_INPUT_PEN_MOVE,
    MEL_INPUT_PEN_UP,
    MEL_INPUT_PEN_PROXIMITY_IN,
    MEL_INPUT_PEN_PROXIMITY_OUT,
    MEL_INPUT_PEN_BUTTON,
};

typedef struct
{
    Mel_Input_Device device;
    u32              phase;
    f32              x, y;
    f32              pressure;
    f32              tilt_x, tilt_y;
    f32              distance;
    f32              rotation;
    f32              tangential_pressure;
    f32              slider;
    u32              buttons;
    u32              button_changed;
    bool             eraser;
    bool             in_proximity;
} Mel_Input_Pen_Event;

typedef enum
{
    MEL_INPUT_EVENT_KEY = 0,
    MEL_INPUT_EVENT_TEXT,
    MEL_INPUT_EVENT_MOUSE,
    MEL_INPUT_EVENT_TOUCH,
    MEL_INPUT_EVENT_PEN,
} Mel_Input_Event_Kind;

typedef struct
{
    Mel_Input_Event_Kind kind;
    union
    {
        Mel_Input_Key_Event   key;
        Mel_Input_Text_Event  text;
        Mel_Input_Mouse_Event mouse;
        Mel_Input_Touch_Event touch;
        Mel_Input_Pen_Event   pen;
    } as;
} Mel_Input_Event;

void mel_input_pump(void);
u32  mel_input_poll(Mel_Input_Event* out, u32 cap);

#ifdef __cplusplus
}
#endif
