#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <gui/style.h>

typedef struct
{
    Mel_Font          font;
    Mel_Style_Color   fg;
    Mel_Style_Surface surface;
} Mel_Button_Style;

static inline bool mel_button_style_any(const Mel_Button_Style* s) { return mel_font_any(&s->font) || s->fg.set || mel_style_surface_any(&s->surface); }

typedef struct
{
    str8                 text;
    i32                  x, y, w, h;
    u32                  id;
    bool                 disabled;
    bool                 hidden;
    void*                user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Pointer_Cb   pointer;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Layoutable       layoutable;
    Mel_Button_Style     style;
} Mel_Button_Opt;

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt opt);
#define mel_button_create(parent, ...) mel_button_create_opt((parent), (Mel_Button_Opt){ __VA_ARGS__ })

void mel_button_set_style_opt(Mel_Gui_Handle h, Mel_Button_Style style);
#define mel_button_set_style(h, ...) mel_button_set_style_opt((h), (Mel_Button_Style){ __VA_ARGS__ })
