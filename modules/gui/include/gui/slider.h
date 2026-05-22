#pragma once

#include <core/types.h>

#include "handle.h"
#include "callbacks.h"

typedef struct {
    void (*on_value_changed)(Mel_Gui_Handle h, i32 value, void* user);
} Mel_Slider_On;

typedef struct {
    i32   x, y, w, h;
    u32   id;
    i32   min_value;
    i32   max_value;
    i32   value;
    bool  disabled;
    bool  hidden;
    void* user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Slider_On        on_;
} Mel_Slider_Opt;

Mel_Gui_Handle mel_slider_create_opt(Mel_Gui_Handle parent, Mel_Slider_Opt opt);
#define mel_slider_create(parent, ...) \
    mel_slider_create_opt((parent), (Mel_Slider_Opt){__VA_ARGS__})

i32  mel_slider_value(Mel_Gui_Handle h);
void mel_slider_set_value(Mel_Gui_Handle h, i32 value);
