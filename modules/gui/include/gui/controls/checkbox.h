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
    Mel_Style_Color   tint;
    Mel_Style_Surface surface;
} Mel_CheckBox_Style;

static inline bool mel_checkbox_style_any(const Mel_CheckBox_Style* s) { return mel_font_any(&s->font) || s->fg.set || s->tint.set || mel_style_surface_any(&s->surface); }

typedef struct
{
    void (*on_toggled)(Mel_Gui_Handle h, bool checked, void* user);
} Mel_CheckBox_On;

typedef struct
{
    str8                 text;
    i32                  x, y, w, h;
    u32                  id;
    bool                 checked;
    bool                 disabled;
    bool                 hidden;
    void*                user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_CheckBox_On      on_;
    Mel_Layoutable       layoutable;
    Mel_CheckBox_Style   style;
} Mel_CheckBox_Opt;

Mel_Gui_Handle mel_checkbox_create_opt(Mel_Gui_Handle parent, Mel_CheckBox_Opt opt);
#define mel_checkbox_create(parent, ...) mel_checkbox_create_opt((parent), (Mel_CheckBox_Opt){ __VA_ARGS__ })

bool mel_checkbox_checked(Mel_Gui_Handle h);

void mel_checkbox_set_style_opt(Mel_Gui_Handle h, Mel_CheckBox_Style style);
#define mel_checkbox_set_style(h, ...) mel_checkbox_set_style_opt((h), (Mel_CheckBox_Style){ __VA_ARGS__ })
