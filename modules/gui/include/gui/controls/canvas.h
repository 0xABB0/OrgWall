#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <paint/painter.h>
#include <gui/style.h>

typedef struct
{
    Mel_Style_Surface surface;
} Mel_Canvas_Style;

static inline bool mel_canvas_style_any(const Mel_Canvas_Style* s) { return mel_style_surface_any(&s->surface); }

typedef struct
{
    void (*on_paint)(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 h_, void* user);
} Mel_Canvas_On;

typedef struct
{
    i32                  x, y, w, h;
    u32                  id;
    bool                 hidden;
    void*                user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Pointer_Cb   pointer;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Canvas_On        on_;
    Mel_Layoutable       layoutable;
    Mel_Canvas_Style     style;
} Mel_Canvas_Opt;

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt opt);
#define mel_canvas_create(parent, ...) mel_canvas_create_opt((parent), (Mel_Canvas_Opt){ __VA_ARGS__ })

void mel_canvas_set_style_opt(Mel_Gui_Handle h, Mel_Canvas_Style style);
#define mel_canvas_set_style(h, ...) mel_canvas_set_style_opt((h), (Mel_Canvas_Style){ __VA_ARGS__ })
