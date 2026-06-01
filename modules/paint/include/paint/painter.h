#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <color/rgba8.h>
#include <math.geo/rect.h>
#include <math.vector/vec2.h>

#include <paint/handle.h>

/* Immediate-mode drawing cursor for one pass against a Mel_Drawable. The
 * concrete type is backend-defined; each op lowers straight to the platform's
 * native 2D API (quartz = CoreGraphics/CoreText), selected at compile time —
 * no vtable, no draw-list. A painter never outlives its begin/end. */
typedef struct Mel_Painter Mel_Painter;

Mel_Painter* mel_painter_begin(Mel_Drawable d);
void         mel_painter_end(Mel_Painter* p);

void mel_painter_clear(Mel_Painter*, mel_color8);
void mel_painter_fill_rect(Mel_Painter*, Mel_Rect, mel_color8);
void mel_painter_fill_ellipse(Mel_Painter*, Mel_Rect, mel_color8);
void mel_painter_stroke_rect(Mel_Painter*, Mel_Rect, mel_color8, f32 width);
void mel_painter_draw_line(Mel_Painter*, Mel_Vec2 a, Mel_Vec2 b, mel_color8, f32 width);
void mel_painter_fill_round_rect(Mel_Painter*, Mel_Rect, f32 radius, mel_color8);
void mel_painter_draw_text(Mel_Painter*, str8 text, Mel_Vec2 pos, mel_color8, f32 size);
