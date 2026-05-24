#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

typedef struct {
    void (*on_result)(Mel_Gui_Handle h, i32 result, void* user);
} Mel_Dialog_On;

typedef struct {
    str8  title;
    i32   x, y, w, h;

    i32   min_w, min_h;
    i32   max_w, max_h;

    bool  not_resizable;
    bool  undecorated;

    Mel_Gui_Handle owner;

    void* user;
    Mel_Dialog_On        on_;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Layout*          layout;
} Mel_Dialog_Opt;

Mel_Gui_Handle mel_dialog_create_opt(Mel_Dialog_Opt opt);
#define mel_dialog_create(...) mel_dialog_create_opt((Mel_Dialog_Opt){__VA_ARGS__})

void mel_dialog_close(Mel_Gui_Handle dialog, i32 result);
