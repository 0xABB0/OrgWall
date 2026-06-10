#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <color/rgba8.h>

#include <gui/handle.h>

/* An optional color: zero (unset) means "the platform's native look", which
 * is the meaningful zero default of every style field. Use the makers below
 * so designated initializers stay terse: .style.fg = mel_style_rgb(...). */
typedef struct
{
    mel_color8 color;
    bool       set;
} Mel_Style_Color;

static inline Mel_Style_Color mel_style_color(mel_color8 c) { return (Mel_Style_Color){ .color = c, .set = true }; }
static inline Mel_Style_Color mel_style_rgb(u8 r, u8 g, u8 b) { return (Mel_Style_Color){ .color = { r, g, b, 255 }, .set = true }; }
static inline Mel_Style_Color mel_style_rgba(u8 r, u8 g, u8 b, u8 a) { return (Mel_Style_Color){ .color = { r, g, b, a }, .set = true }; }

/* Visual customization shared by every widget family, native and drawn. Each
 * backend maps every field it can express natively and emulates a missing
 * property in the narrowest way that property allows (a rounded corner on a
 * platform without one rounds that widget's surface; it never escalates the
 * widget to custom-drawn). What a backend can honestly do is documented in
 * the module readme's per-backend matrix. */
typedef struct
{
    str8 font_family; /* empty: native default */
    f32  font_size;   /* 0: native default; logical points */
    u16  font_weight; /* 0: native default; 100..900, 400 normal, 700 bold */
    bool italic;

    Mel_Style_Color fg;
    Mel_Style_Color bg;
    Mel_Style_Color border_color;
    f32             border_width;  /* 0: no border */
    f32             corner_radius; /* 0: the native corner */

    i32 padding_l, padding_t, padding_r, padding_b;
} Mel_Style;

/* Apply the set fields of `style` to a live widget. Fields left at zero do
 * not reset earlier applications; a reset-to-native op is deferred. Defined
 * by the backend, like every widget op. */
void mel_gui_set_style(Mel_Gui_Handle h, Mel_Style style);

static inline bool mel_style_any(const Mel_Style* s)
{
    return s->font_family.len || s->font_size || s->font_weight || s->italic || s->fg.set || s->bg.set || s->border_color.set || s->border_width || s->corner_radius || s->padding_l || s->padding_t || s->padding_r || s->padding_b;
}
