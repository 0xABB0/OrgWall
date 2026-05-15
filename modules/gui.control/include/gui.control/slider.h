#pragma once

#include <gui/gui.h>

#define MEL_GUI_CLASS_SLIDER S8("mel.slider")

static inline Mel_Gui_Handle mel_gui_create_slider_proc(Mel_Gui_Handle parent, u32 id, i32 x, i32 y, i32 w, i32 h, Mel_Gui_Proc proc, void* user)
{
    Mel_Gui_Create_Desc desc = {
        .class_name = MEL_GUI_CLASS_SLIDER,
        .style = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE | MEL_GUI_WS_TABSTOP,
        .x = x,
        .y = y,
        .w = w,
        .h = h,
        .parent = parent,
        .id = id,
        .proc = proc,
        .user = user,
    };
    return mel_gui_create(&desc);
}
