#pragma once

#include "types.h"
#include "math.vec2.h"

typedef struct
{
    f32 x, y, w, h;
} Mel_Rect;

[[nodiscard]] static inline Mel_Rect mel_rect(f32 x, f32 y, f32 w, f32 h);
[[nodiscard]] static inline Mel_Rect mel_rect_from_pos_size(Mel_Vec2 pos, Mel_Vec2 size);
[[nodiscard]] static inline Mel_Vec2 mel_rect_pos(Mel_Rect r);
[[nodiscard]] static inline Mel_Vec2 mel_rect_size(Mel_Rect r);
[[nodiscard]] static inline Mel_Vec2 mel_rect_center(Mel_Rect r);
[[nodiscard]] static inline Mel_Vec2 mel_rect_min(Mel_Rect r);
[[nodiscard]] static inline Mel_Vec2 mel_rect_max(Mel_Rect r);
[[nodiscard]] static inline bool mel_rect_contains_point(Mel_Rect r, Mel_Vec2 p);
[[nodiscard]] static inline bool mel_rect_overlaps(Mel_Rect a, Mel_Rect b);
[[nodiscard]] static inline Mel_Rect mel_rect_intersection(Mel_Rect a, Mel_Rect b);
[[nodiscard]] static inline Mel_Rect mel_rect_expand(Mel_Rect r, f32 amount);
[[nodiscard]] static inline Mel_Rect mel_rect_translate(Mel_Rect r, Mel_Vec2 offset);
[[nodiscard]] static inline Mel_Rect mel_rect_from_center_extents(Mel_Vec2 center, Mel_Vec2 extents);
[[nodiscard]] static inline f32 mel_rect_width(Mel_Rect r);
[[nodiscard]] static inline f32 mel_rect_height(Mel_Rect r);
[[nodiscard]] static inline Mel_Vec2 mel_rect_extents(Mel_Rect r);
[[nodiscard]] static inline Mel_Vec2 mel_rect_corner(Mel_Rect r, i32 index);
static inline void mel_rect_corners(Mel_Rect r, Mel_Vec2 out[4]);
[[nodiscard]] static inline Mel_Rect mel_rect_move(Mel_Rect r, Mel_Vec2 offset);
[[nodiscard]] static inline Mel_Rect mel_rect_empty(void);
[[nodiscard]] static inline bool mel_rect_is_empty(Mel_Rect r);
static inline void mel_rect_add_point(Mel_Rect* r, Mel_Vec2 p);

#include "math.geo.rect.inl"
