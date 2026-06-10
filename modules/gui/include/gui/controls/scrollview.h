#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <gui/style.h>

typedef struct
{
    Mel_Style_Surface surface;
} Mel_ScrollView_Style;

static inline bool mel_scrollview_style_any(const Mel_ScrollView_Style* s) { return mel_style_surface_any(&s->surface); }

typedef struct
{
    i32                  x, y, w, h;
    i32                  content_w, content_h;
    u32                  id;
    bool                 disabled;
    bool                 hidden;
    void*                user;
    Mel_Gui_Focus_Cb     focus;
    Mel_Layout*          layout;
    Mel_Layoutable       layoutable;
    Mel_ScrollView_Style style;
} Mel_ScrollView_Opt;

Mel_Gui_Handle mel_scrollview_create_opt(Mel_Gui_Handle parent, Mel_ScrollView_Opt opt);
#define mel_scrollview_create(parent, ...) mel_scrollview_create_opt((parent), (Mel_ScrollView_Opt){ __VA_ARGS__ })

void mel_scrollview_set_style_opt(Mel_Gui_Handle h, Mel_ScrollView_Style style);
#define mel_scrollview_set_style(h, ...) mel_scrollview_set_style_opt((h), (Mel_ScrollView_Style){ __VA_ARGS__ })
