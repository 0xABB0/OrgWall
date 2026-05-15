#pragma once

#include <gui/gui.h>

#define MEL_GUI_CLASS_WINDOW S8("mel.window")

static inline Mel_Gui_Handle mel_gui_create_window(str8 title, i32 w, i32 h, Mel_Gui_Proc proc, void* user)
{
    Mel_Gui_Create_Desc desc = {
        .class_name = MEL_GUI_CLASS_WINDOW,
        .text = title,
        .style = MEL_GUI_WS_WINDOW | MEL_GUI_WS_VISIBLE,
        .x = (i32)MEL_GUI_USE_DEFAULT,
        .y = (i32)MEL_GUI_USE_DEFAULT,
        .w = w,
        .h = h,
        .proc = proc,
        .user = user,
    };
    return mel_gui_create(&desc);
}
