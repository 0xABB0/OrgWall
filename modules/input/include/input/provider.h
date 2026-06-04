#pragma once

#include <input/input.h>
#include <input/events.h>
#include <input/keyboard.h>
#include <input/mouse.h>
#include <input/touch.h>
#include <input/pen.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64                         stable_id;
    Mel_Input_Device_Descriptor desc;
} Mel_Input_Raw;

typedef struct Mel_Input_Sink Mel_Input_Sink;

void mel_input_sink_key(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Key_Event* ev);
void mel_input_sink_text(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Text_Event* ev);
void mel_input_sink_mouse(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Mouse_Event* ev);
void mel_input_sink_touch(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Touch_Event* ev);
void mel_input_sink_pen(Mel_Input_Sink* sink, u64 stable_id, const Mel_Input_Pen_Event* ev);

typedef struct
{
    const char* name;
    void*       user;

    u32 (*enumerate)(void* user, Mel_Input_Raw* out, u32 cap);
    void (*pump)(void* user, Mel_Input_Sink* sink);

    bool (*key_down)(void* user, u64 stable_id, Mel_Scancode sc);
    u32 (*modifiers)(void* user, u64 stable_id);
    Mel_Keycode (*keycode_from_scancode)(void* user, u64 stable_id, Mel_Scancode sc);
    Mel_Scancode (*scancode_from_keycode)(void* user, u64 stable_id, Mel_Keycode kc);

    Mel_Mouse_State (*mouse_state)(void* user, u64 stable_id);
    Mel_Input_Status (*mouse_set_relative)(void* user, u64 stable_id, bool enable);
    Mel_Input_Status (*mouse_capture)(void* user, bool enable);
    Mel_Input_Status (*mouse_warp)(void* user, u64 stable_id, f32 x, f32 y, bool global);
    Mel_Input_Status (*mouse_confine)(void* user, const Mel_Mouse_Rect* rect);

    Mel_Touch_State (*touch_state)(void* user, u64 stable_id);
    u32 (*touch_fingers)(void* user, u64 stable_id, Mel_Touch_Finger* out, u32 cap);

    Mel_Pen_State (*pen_state)(void* user, u64 stable_id);

    Mel_Cursor (*cursor_create_system)(void* user, Mel_Cursor_Shape shape);
    Mel_Cursor (*cursor_create_custom)(void* user, const Mel_Cursor_Opt* opt);
    void (*cursor_destroy)(void* user, Mel_Cursor c);
    Mel_Input_Status (*cursor_set)(void* user, Mel_Cursor c);
    Mel_Input_Status (*cursor_show)(void* user, bool visible);

    Mel_Input_Status (*text_start)(void* user, const Mel_Input_Text_Opt* opt);
    void (*text_stop)(void* user);
    Mel_Input_Status (*text_set_area)(void* user, Mel_Input_Rect area);
    Mel_Input_Status (*osk_show)(void* user);
    Mel_Input_Status (*osk_hide)(void* user);

    void* (*native)(void* user, u64 stable_id);
} Mel_Input_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Input_Provider;

Mel_Input_Provider mel_input_provider_register(const Mel_Input_Provider_Desc* desc);
void               mel_input_provider_unregister(Mel_Input_Provider p);

void mel_input__register_host_providers(void);

#ifdef __cplusplus
}
#endif
