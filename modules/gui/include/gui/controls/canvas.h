#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <gui/painter.h>

typedef struct {
    void (*on_paint)(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 h_, void* user);
} Mel_Canvas_On;

typedef struct {
    i32   x, y, w, h;
    u32   id;
    bool  hidden;
    void* user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Pointer_Cb   pointer;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Canvas_On        on_;
    Mel_Layoutable       layoutable;
} Mel_Canvas_Opt;

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt opt);
#define mel_canvas_create(parent, ...) \
    mel_canvas_create_opt((parent), (Mel_Canvas_Opt){__VA_ARGS__})
