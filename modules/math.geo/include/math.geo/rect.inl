#ifdef _CLANGD
#pragma once
#include "rect.h"
#endif

static inline Mel_Rect mel_rect(f32 x, f32 y, f32 w, f32 h)
{
    return (Mel_Rect){x, y, w, h};
}

static inline Mel_Rect mel_rect_from_pos_size(Mel_Vec2 pos, Mel_Vec2 size)
{
    return (Mel_Rect){pos.x, pos.y, size.w, size.h};
}

static inline Mel_Vec2 mel_rect_pos(Mel_Rect r)
{
    return mel_vec2(r.x, r.y);
}

static inline Mel_Vec2 mel_rect_size(Mel_Rect r)
{
    return mel_vec2(r.w, r.h);
}

static inline Mel_Vec2 mel_rect_center(Mel_Rect r)
{
    return mel_vec2(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
}

static inline Mel_Vec2 mel_rect_min(Mel_Rect r)
{
    return mel_vec2(r.x, r.y);
}

static inline Mel_Vec2 mel_rect_max(Mel_Rect r)
{
    return mel_vec2(r.x + r.w, r.y + r.h);
}

static inline bool mel_rect_contains_point(Mel_Rect r, Mel_Vec2 p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

static inline bool mel_rect_overlaps(Mel_Rect a, Mel_Rect b)
{
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

static inline Mel_Rect mel_rect_intersection(Mel_Rect a, Mel_Rect b)
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

static inline Mel_Rect mel_rect_expand(Mel_Rect r, f32 amount)
{
    return (Mel_Rect){r.x - amount, r.y - amount, r.w + amount * 2, r.h + amount * 2};
}

static inline Mel_Rect mel_rect_translate(Mel_Rect r, Mel_Vec2 offset)
{
    return (Mel_Rect){r.x + offset.x, r.y + offset.y, r.w, r.h};
}

static inline Mel_Rect mel_rect_from_center_extents(Mel_Vec2 center, Mel_Vec2 extents)
{
    return (Mel_Rect){center.x - extents.x, center.y - extents.y, extents.x * 2.0f, extents.y * 2.0f};
}

static inline f32 mel_rect_width(Mel_Rect r)
{
    return r.w;
}

static inline f32 mel_rect_height(Mel_Rect r)
{
    return r.h;
}

static inline Mel_Vec2 mel_rect_extents(Mel_Rect r)
{
    return mel_vec2(r.w * 0.5f, r.h * 0.5f);
}

static inline Mel_Vec2 mel_rect_corner(Mel_Rect r, i32 index)
{
    f32 x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    switch (index)
    {
    case 0: return mel_vec2(x0, y0);
    case 1: return mel_vec2(x1, y0);
    case 2: return mel_vec2(x1, y1);
    case 3: return mel_vec2(x0, y1);
    default: return mel_vec2(x0, y0);
    }
}

static inline void mel_rect_corners(Mel_Rect r, Mel_Vec2 out[4])
{
    out[0] = mel_vec2(r.x, r.y);
    out[1] = mel_vec2(r.x + r.w, r.y);
    out[2] = mel_vec2(r.x + r.w, r.y + r.h);
    out[3] = mel_vec2(r.x, r.y + r.h);
}

static inline Mel_Rect mel_rect_move(Mel_Rect r, Mel_Vec2 offset)
{
    return (Mel_Rect){r.x + offset.x, r.y + offset.y, r.w, r.h};
}

static inline Mel_Rect mel_rect_empty(void)
{
    return (Mel_Rect){0, 0, 0, 0};
}

static inline bool mel_rect_is_empty(Mel_Rect r)
{
    return r.w <= 0.0f || r.h <= 0.0f;
}

static inline void mel_rect_add_point(Mel_Rect* r, Mel_Vec2 p)
{
    if (mel_rect_is_empty(*r))
    {
        r->x = p.x;
        r->y = p.y;
        r->w = 0;
        r->h = 0;
        return;
    }
    f32 x0 = r->x < p.x ? r->x : p.x;
    f32 y0 = r->y < p.y ? r->y : p.y;
    f32 x1 = (r->x + r->w) > p.x ? (r->x + r->w) : p.x;
    f32 y1 = (r->y + r->h) > p.y ? (r->y + r->h) : p.y;
    r->x = x0;
    r->y = y0;
    r->w = x1 - x0;
    r->h = y1 - y0;
}
