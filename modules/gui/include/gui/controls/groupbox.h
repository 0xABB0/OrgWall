#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

typedef struct {
    str8  title;
    i32   x, y, w, h;
    u32   id;
    bool  disabled;
    bool  hidden;
    void* user;
    Mel_Gui_Focus_Cb     focus;
    Mel_Layout*          layout;
    Mel_Layoutable       layoutable;
} Mel_GroupBox_Opt;

Mel_Gui_Handle mel_groupbox_create_opt(Mel_Gui_Handle parent, Mel_GroupBox_Opt opt);
#define mel_groupbox_create(parent, ...) \
    mel_groupbox_create_opt((parent), (Mel_GroupBox_Opt){__VA_ARGS__})
