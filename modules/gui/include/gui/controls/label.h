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
} Mel_Label_Style;

static inline bool mel_label_style_any(const Mel_Label_Style* s) { return mel_font_any(&s->font) || s->fg.set || mel_style_surface_any(&s->surface); }

typedef struct
{
    str8                 text;
    i32                  x, y, w, h;
    u32                  id;
    bool                 hidden;
    void*                user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Layoutable       layoutable;
    Mel_Label_Style      style;
} Mel_Label_Opt;

Mel_Gui_Handle mel_label_create_opt(Mel_Gui_Handle parent, Mel_Label_Opt opt);
#define mel_label_create(parent, ...) mel_label_create_opt((parent), (Mel_Label_Opt){ __VA_ARGS__ })

void mel_label_set_style_opt(Mel_Gui_Handle h, Mel_Label_Style style);
#define mel_label_set_style(h, ...) mel_label_set_style_opt((h), (Mel_Label_Style){ __VA_ARGS__ })
