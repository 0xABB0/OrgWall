#pragma once

#include <gui/gui.h>

#define MEL_GUI_CLASS_LABEL S8("mel.label")

static inline Mel_Gui_Handle mel_gui_create_label(Mel_Gui_Handle parent, str8 text, u32 id, i32 x, i32 y, i32 w, i32 h)
{
    Mel_Gui_Create_Desc desc = {
        .class_name = MEL_GUI_CLASS_LABEL,
        .text = text,
        .style = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE,
        .x = x,
        .y = y,
        .w = w,
        .h = h,
        .parent = parent,
        .id = id,
    };
    return mel_gui_create(&desc);
}
