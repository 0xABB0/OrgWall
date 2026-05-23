#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>

typedef enum {
    MEL_FRAME_NORMAL = 0,
    MEL_FRAME_MINIMIZED,
    MEL_FRAME_MAXIMIZED,
    MEL_FRAME_HIDDEN,
} Mel_Frame_State;

typedef struct {
    str8  title;
    i32   x, y, w, h;

    i32   min_w, min_h;
    i32   max_w, max_h;

    bool  not_resizable;
    bool  undecorated;
    bool  not_closable;

    Mel_Gui_Handle  owner;
    void*           icon_large;
    void*           icon_small;
    Mel_Frame_State initial_state;

    void* user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Keyboard_Cb  keyboard;
} Mel_Frame_Opt;

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt opt);
#define mel_frame_create(...) mel_frame_create_opt((Mel_Frame_Opt){__VA_ARGS__})
