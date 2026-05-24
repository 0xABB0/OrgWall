#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

typedef struct {
    i32   x, y, w, h;
    u32   id;
    bool  disabled;
    bool  hidden;
    void* user;
    Mel_Gui_Pointer_Cb   pointer;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Layout*          layout;
    Mel_Layoutable       layoutable;
} Mel_Panel_Opt;

Mel_Gui_Handle mel_panel_create_opt(Mel_Gui_Handle parent, Mel_Panel_Opt opt);
#define mel_panel_create(parent, ...) \
    mel_panel_create_opt((parent), (Mel_Panel_Opt){__VA_ARGS__})
