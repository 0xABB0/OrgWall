#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

typedef struct {
    str8  text;
    i32   x, y, w, h;
    u32   id;
    bool  hidden;
    void* user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Layoutable       layoutable;
} Mel_Label_Opt;

Mel_Gui_Handle mel_label_create_opt(Mel_Gui_Handle parent, Mel_Label_Opt opt);
#define mel_label_create(parent, ...) \
    mel_label_create_opt((parent), (Mel_Label_Opt){__VA_ARGS__})
