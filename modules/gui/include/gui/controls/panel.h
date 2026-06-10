#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <gui/style.h>

typedef struct
{
    Mel_Style_Surface surface;
} Mel_Panel_Style;

static inline bool mel_panel_style_any(const Mel_Panel_Style* s) { return mel_style_surface_any(&s->surface); }

typedef struct
{
    i32                 x, y, w, h;
    u32                 id;
    bool                disabled;
    bool                hidden;
    void*               user;
    Mel_Gui_Pointer_Cb  pointer;
    Mel_Gui_Focus_Cb    focus;
    Mel_Gui_Keyboard_Cb keyboard;
    Mel_Layout*         layout;
    Mel_Layoutable      layoutable;
    Mel_Panel_Style     style;
} Mel_Panel_Opt;

Mel_Gui_Handle mel_panel_create_opt(Mel_Gui_Handle parent, Mel_Panel_Opt opt);
#define mel_panel_create(parent, ...) mel_panel_create_opt((parent), (Mel_Panel_Opt){ __VA_ARGS__ })

void mel_panel_set_style_opt(Mel_Gui_Handle h, Mel_Panel_Style style);
#define mel_panel_set_style(h, ...) mel_panel_set_style_opt((h), (Mel_Panel_Style){ __VA_ARGS__ })
