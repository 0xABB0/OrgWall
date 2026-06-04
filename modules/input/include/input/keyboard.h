#pragma once

#include <input/input.h>
#include <input/scancode.h>
#include <string/str8.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_Input_Device device;
    u32              modifiers;
    u32              pressed_count;
} Mel_Keyboard_State;

bool mel_keyboard_key_down(Mel_Input_Device d, Mel_Scancode sc);
u32  mel_keyboard_modifiers(Mel_Input_Device d);

Mel_Scancode mel_keyboard_scancode_from_keycode(Mel_Input_Device d, Mel_Keycode kc);
Mel_Keycode  mel_keyboard_keycode_from_scancode(Mel_Input_Device d, Mel_Scancode sc);

str8 mel_keyboard_scancode_name(Mel_Scancode sc);
str8 mel_keyboard_key_name(Mel_Input_Device d, Mel_Keycode kc, char* buf, usize buf_size);

enum
{
    MEL_INPUT_TYPE_TEXT = 0,
    MEL_INPUT_TYPE_EMAIL,
    MEL_INPUT_TYPE_PASSWORD,
    MEL_INPUT_TYPE_NUMBER,
    MEL_INPUT_TYPE_PIN,
    MEL_INPUT_TYPE_URL,
    MEL_INPUT_TYPE_PHONE,
    MEL_INPUT_TYPE_SEARCH,
};

typedef struct
{
    i32 x, y, w, h;
} Mel_Input_Rect;

typedef struct
{
    u32            input_type;
    Mel_Input_Rect area;
    i32            cursor;
    bool           autocorrect;
    bool           multiline;
} Mel_Input_Text_Opt;

Mel_Input_Status mel_input_text_start_opt(Mel_Input_Text_Opt opt);
#define mel_input_text_start(...) mel_input_text_start_opt((Mel_Input_Text_Opt){ .input_type = MEL_INPUT_TYPE_TEXT, __VA_ARGS__ })

void             mel_input_text_stop(void);
bool             mel_input_text_active(void);
Mel_Input_Status mel_input_text_set_area(Mel_Input_Rect area);

Mel_Input_Status mel_input_osk_show(void);
Mel_Input_Status mel_input_osk_hide(void);
bool             mel_input_osk_visible(void);

#ifdef __cplusplus
}
#endif
