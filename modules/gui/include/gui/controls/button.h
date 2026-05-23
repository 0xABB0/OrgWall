#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

typedef struct {
    str8  text;
    i32   x, y, w, h;
    u32   id;
    bool  disabled;
    bool  hidden;
    void* user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Pointer_Cb   pointer;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Layoutable       layoutable;
} Mel_Button_Opt;

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt opt);
#define mel_button_create(parent, ...) \
    mel_button_create_opt((parent), (Mel_Button_Opt){__VA_ARGS__})
