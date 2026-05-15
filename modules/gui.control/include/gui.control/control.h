#pragma once

#include "window.h"
#include "panel.h"
#include "label.h"
#include "button.h"
#include "edit.h"
#include "checkbox.h"
#include "slider.h"

static inline Mel_Gui_Handle mel_gui_create_child_proc(
    Mel_Gui_Handle parent, str8 class_name, str8 text, u32 id, i32 x, i32 y, i32 w, i32 h, Mel_Gui_Proc proc, void* user)
{
    Mel_Gui_Create_Desc desc = {
        .class_name = class_name,
        .text = text,
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

static inline Mel_Gui_Handle mel_gui_create_child(
    Mel_Gui_Handle parent, str8 class_name, str8 text, u32 id, i32 x, i32 y, i32 w, i32 h)
{
    return mel_gui_create_child_proc(parent, class_name, text, id, x, y, w, h, NULL, NULL);
}
