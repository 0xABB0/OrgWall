#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <gui/style.h>

typedef struct
{
    Mel_Style_Color   track;
    Mel_Style_Color   thumb;
    Mel_Style_Surface surface;
} Mel_Slider_Style;

static inline bool mel_slider_style_any(const Mel_Slider_Style* s) { return s->track.set || s->thumb.set || mel_style_surface_any(&s->surface); }

typedef struct
{
    void (*on_value_changed)(Mel_Gui_Handle h, i32 value, void* user);
} Mel_Slider_On;

typedef struct
{
    i32                  x, y, w, h;
    u32                  id;
    i32                  min_value;
    i32                  max_value;
    i32                  value;
    bool                 disabled;
    bool                 hidden;
    void*                user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Slider_On        on_;
    Mel_Layoutable       layoutable;
    Mel_Slider_Style     style;
} Mel_Slider_Opt;

Mel_Gui_Handle mel_slider_create_opt(Mel_Gui_Handle parent, Mel_Slider_Opt opt);
#define mel_slider_create(parent, ...) mel_slider_create_opt((parent), (Mel_Slider_Opt){ __VA_ARGS__ })

i32  mel_slider_value(Mel_Gui_Handle h);
void mel_slider_set_value(Mel_Gui_Handle h, i32 value);

void mel_slider_set_style_opt(Mel_Gui_Handle h, Mel_Slider_Style style);
#define mel_slider_set_style(h, ...) mel_slider_set_style_opt((h), (Mel_Slider_Style){ __VA_ARGS__ })
