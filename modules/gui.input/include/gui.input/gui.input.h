#pragma once

#include <gui/gui.h>

typedef enum Mel_Gui_Pointer_Kind {
    MEL_GUI_POINTER_MOUSE,
    MEL_GUI_POINTER_TOUCH,
    MEL_GUI_POINTER_PEN,
    MEL_GUI_POINTER_GAZE,
    MEL_GUI_POINTER_CONTROLLER,
} Mel_Gui_Pointer_Kind;

typedef enum Mel_Gui_Key_Mod {
    MEL_GUI_KEY_MOD_NONE  = 0,
    MEL_GUI_KEY_MOD_SHIFT = 1u << 0,
    MEL_GUI_KEY_MOD_CTRL  = 1u << 1,
    MEL_GUI_KEY_MOD_ALT   = 1u << 2,
    MEL_GUI_KEY_MOD_SUPER = 1u << 3,
} Mel_Gui_Key_Mod;

typedef struct Mel_Gui_Pointer_Event {
    Mel_Gui_Pointer_Kind kind;
    u32 pointer_id;
    i32 x;
    i32 y;
    f32 pressure;
    u32 buttons;
    u32 modifiers;
} Mel_Gui_Pointer_Event;

typedef struct Mel_Gui_Key_Event {
    u32 key;
    u32 scan;
    u32 modifiers;
    bool repeat;
} Mel_Gui_Key_Event;
