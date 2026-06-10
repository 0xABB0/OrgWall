#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <color/rgba8.h>

#include <gui/handle.h>

/* Styling primitives. Every widget defines its own Mel_<Widget>_Style in its
 * control header, composed from these — a widget exposes exactly the visual
 * properties it supports, plus its widget-specific ones (a slider's track and
 * thumb, a groupbox's title). The zero value of every field means "the
 * platform's native look"; setters apply set fields and never reset unset
 * ones. */

typedef struct
{
    mel_color8 color;
    bool       set;
} Mel_Style_Color;

static inline Mel_Style_Color mel_style_color(mel_color8 c) { return (Mel_Style_Color){ .color = c, .set = true }; }
static inline Mel_Style_Color mel_style_rgb(u8 r, u8 g, u8 b) { return (Mel_Style_Color){ .color = { r, g, b, 255 }, .set = true }; }
static inline Mel_Style_Color mel_style_rgba(u8 r, u8 g, u8 b, u8 a) { return (Mel_Style_Color){ .color = { r, g, b, a }, .set = true }; }

typedef struct
{
    str8 family; /* empty: native default */
    f32  size;   /* 0: native default; logical points */
    u16  weight; /* 0: native default; 100..900, 400 normal, 700 bold */
    bool italic;
} Mel_Font;

/* The box around a widget's content: background, border, corner, padding. */
typedef struct
{
    Mel_Style_Color bg;
    Mel_Style_Color border_color;
    f32             border_width;  /* 0: no border */
    f32             corner_radius; /* 0: the native corner */
    i32             padding_l, padding_t, padding_r, padding_b;
} Mel_Style_Surface;

static inline bool mel_font_any(const Mel_Font* f) { return f->family.len || f->size || f->weight || f->italic; }

static inline bool mel_style_surface_any(const Mel_Style_Surface* s)
{
    return s->bg.set || s->border_color.set || s->border_width || s->corner_radius || s->padding_l || s->padding_t || s->padding_r || s->padding_b;
}
