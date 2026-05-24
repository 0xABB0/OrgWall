#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

typedef struct {
    i32   x, y, w, h;
    i32   content_w, content_h;
    u32   id;
    bool  disabled;
    bool  hidden;
    void* user;
    Mel_Gui_Focus_Cb     focus;
    Mel_Layout*          layout;
    Mel_Layoutable       layoutable;
} Mel_ScrollView_Opt;

Mel_Gui_Handle mel_scrollview_create_opt(Mel_Gui_Handle parent, Mel_ScrollView_Opt opt);
#define mel_scrollview_create(parent, ...) \
    mel_scrollview_create_opt((parent), (Mel_ScrollView_Opt){__VA_ARGS__})
