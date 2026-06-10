#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <gui/style.h>

typedef struct
{
    Mel_Font          title_font;
    Mel_Style_Color   title_fg;
    Mel_Style_Surface surface;
} Mel_GroupBox_Style;

static inline bool mel_groupbox_style_any(const Mel_GroupBox_Style* s) { return mel_font_any(&s->title_font) || s->title_fg.set || mel_style_surface_any(&s->surface); }

typedef struct
{
    str8               title;
    i32                x, y, w, h;
    u32                id;
    bool               disabled;
    bool               hidden;
    void*              user;
    Mel_Gui_Focus_Cb   focus;
    Mel_Layout*        layout;
    Mel_Layoutable     layoutable;
    Mel_GroupBox_Style style;
} Mel_GroupBox_Opt;

Mel_Gui_Handle mel_groupbox_create_opt(Mel_Gui_Handle parent, Mel_GroupBox_Opt opt);
#define mel_groupbox_create(parent, ...) mel_groupbox_create_opt((parent), (Mel_GroupBox_Opt){ __VA_ARGS__ })

void mel_groupbox_set_style_opt(Mel_Gui_Handle h, Mel_GroupBox_Style style);
#define mel_groupbox_set_style(h, ...) mel_groupbox_set_style_opt((h), (Mel_GroupBox_Style){ __VA_ARGS__ })
