#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <math.geo/rect.h>
#include <math.vector/vec2.h>

#include <gui/color.h>

/* Immediate-mode drawing surface handed to a Canvas on_paint. The concrete
 * type is backend-defined (a wrapped HDC / CGContextRef / android.graphics
 * Canvas); the app only ever holds the pointer and passes it to mel_painter_*.
 * Each op translates straight to the native 2D API, defined in src/<backend>/,
 * selected at compile time — no vtable, no draw-list, no heap allocation on a
 * native backend. The drawn backend will later implement the same surface by
 * recording into a draw-list its renderer plays back. */
typedef struct Mel_Painter Mel_Painter;

void mel_painter_clear       (Mel_Painter*, Mel_Color);
void mel_painter_fill_rect   (Mel_Painter*, Mel_Rect, Mel_Color);
void mel_painter_fill_ellipse(Mel_Painter*, Mel_Rect, Mel_Color);
void mel_painter_stroke_rect (Mel_Painter*, Mel_Rect, Mel_Color, f32 width);
void mel_painter_draw_line   (Mel_Painter*, Mel_Vec2 a, Mel_Vec2 b, Mel_Color, f32 width);
void mel_painter_fill_round_rect(Mel_Painter*, Mel_Rect, f32 radius, Mel_Color);
void mel_painter_draw_text   (Mel_Painter*, str8 text, Mel_Vec2 pos, Mel_Color, f32 size);
