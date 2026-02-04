#ifndef MEL_RECT_H
#define MEL_RECT_H

#include "types.h"
#include "vec2.h"

typedef struct
{
    f32 x, y, w, h;
} Mel_Rect;

[[nodiscard]] static inline Mel_Rect mel_rect(f32 x, f32 y, f32 w, f32 h)
{
    return (Mel_Rect){x, y, w, h};
}

[[nodiscard]] static inline Mel_Rect mel_rect_from_pos_size(Mel_Vec2 pos, Mel_Vec2 size)
{
    return (Mel_Rect){pos.x, pos.y, size.w, size.h};
}

[[nodiscard]] static inline Mel_Vec2 mel_rect_pos(Mel_Rect r)
{
    return mel_vec2(r.x, r.y);
}

[[nodiscard]] static inline Mel_Vec2 mel_rect_size(Mel_Rect r)
{
    return mel_vec2(r.w, r.h);
}

[[nodiscard]] static inline Mel_Vec2 mel_rect_center(Mel_Rect r)
{
    return mel_vec2(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
}

[[nodiscard]] static inline Mel_Vec2 mel_rect_min(Mel_Rect r)
{
    return mel_vec2(r.x, r.y);
}

[[nodiscard]] static inline Mel_Vec2 mel_rect_max(Mel_Rect r)
{
    return mel_vec2(r.x + r.w, r.y + r.h);
}

[[nodiscard]] static inline bool mel_rect_contains_point(Mel_Rect r, Mel_Vec2 p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

[[nodiscard]] static inline bool mel_rect_overlaps(Mel_Rect a, Mel_Rect b)
{
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

[[nodiscard]] static inline Mel_Rect mel_rect_intersection(Mel_Rect a, Mel_Rect b)
{
    f32 x1 = a.x > b.x ? a.x : b.x;
    f32 y1 = a.y > b.y ? a.y : b.y;
    f32 x2 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    f32 y2 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);

    f32 w = x2 - x1;
    f32 h = y2 - y1;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    return (Mel_Rect){x1, y1, w, h};
}

[[nodiscard]] static inline Mel_Rect mel_rect_expand(Mel_Rect r, f32 amount)
{
    return (Mel_Rect){r.x - amount, r.y - amount, r.w + amount * 2, r.h + amount * 2};
}

[[nodiscard]] static inline Mel_Rect mel_rect_translate(Mel_Rect r, Mel_Vec2 offset)
{
    return (Mel_Rect){r.x + offset.x, r.y + offset.y, r.w, r.h};
}

#endif
