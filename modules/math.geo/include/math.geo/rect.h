#pragma once

#include <core/compiler.h>

#include "core.types.h"
#include "math.vec2.h"

typedef struct
{
    f32 x, y, w, h;
} Mel_Rect;

MEL_NODISCARD static inline Mel_Rect mel_rect(f32 x, f32 y, f32 w, f32 h);
MEL_NODISCARD static inline Mel_Rect mel_rect_from_pos_size(Mel_Vec2 pos, Mel_Vec2 size);
MEL_NODISCARD static inline Mel_Vec2 mel_rect_pos(Mel_Rect r);
MEL_NODISCARD static inline Mel_Vec2 mel_rect_size(Mel_Rect r);
MEL_NODISCARD static inline Mel_Vec2 mel_rect_center(Mel_Rect r);
MEL_NODISCARD static inline Mel_Vec2 mel_rect_min(Mel_Rect r);
MEL_NODISCARD static inline Mel_Vec2 mel_rect_max(Mel_Rect r);
MEL_NODISCARD static inline bool mel_rect_contains_point(Mel_Rect r, Mel_Vec2 p);
MEL_NODISCARD static inline bool mel_rect_overlaps(Mel_Rect a, Mel_Rect b);
MEL_NODISCARD static inline Mel_Rect mel_rect_intersection(Mel_Rect a, Mel_Rect b);
MEL_NODISCARD static inline Mel_Rect mel_rect_expand(Mel_Rect r, f32 amount);
MEL_NODISCARD static inline Mel_Rect mel_rect_translate(Mel_Rect r, Mel_Vec2 offset);
MEL_NODISCARD static inline Mel_Rect mel_rect_from_center_extents(Mel_Vec2 center, Mel_Vec2 extents);
MEL_NODISCARD static inline f32 mel_rect_width(Mel_Rect r);
MEL_NODISCARD static inline f32 mel_rect_height(Mel_Rect r);
MEL_NODISCARD static inline Mel_Vec2 mel_rect_extents(Mel_Rect r);
MEL_NODISCARD static inline Mel_Vec2 mel_rect_corner(Mel_Rect r, i32 index);
static inline void mel_rect_corners(Mel_Rect r, Mel_Vec2 out[4]);
MEL_NODISCARD static inline Mel_Rect mel_rect_move(Mel_Rect r, Mel_Vec2 offset);
MEL_NODISCARD static inline Mel_Rect mel_rect_empty(void);
MEL_NODISCARD static inline bool mel_rect_is_empty(Mel_Rect r);
static inline void mel_rect_add_point(Mel_Rect* r, Mel_Vec2 p);

#define MEL_UV_FULL mel_rect(0, 0, 1, 1)

#include "rect.inl"
