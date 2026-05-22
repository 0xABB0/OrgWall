#pragma once

#include <core/types.h>
#include <string/str8.h>

#include "handle.h"
#include "callbacks.h"

typedef struct {
    void (*on_text_changed)(Mel_Gui_Handle h, str8 text, void* user);
} Mel_TextField_On;

typedef struct {
    str8  text;
    i32   x, y, w, h;
    u32   id;
    bool  disabled;
    bool  hidden;
    void* user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_TextField_On     on_;
} Mel_TextField_Opt;

Mel_Gui_Handle mel_textfield_create_opt(Mel_Gui_Handle parent, Mel_TextField_Opt opt);
#define mel_textfield_create(parent, ...) \
    mel_textfield_create_opt((parent), (Mel_TextField_Opt){__VA_ARGS__})
