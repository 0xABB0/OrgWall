#ifdef _CLANGD
#pragma once
#include "math.geo.irect.h"
#endif

static inline Mel_IRect mel_irect(i32 x, i32 y, i32 w, i32 h)
{
    return (Mel_IRect){x, y, w, h};
}

static inline Mel_IRect mel_irect_from_minmax(i32 xmin, i32 ymin, i32 xmax, i32 ymax)
{
    return (Mel_IRect){xmin, ymin, xmax - xmin, ymax - ymin};
}

static inline Mel_IRect mel_irect_from_pos_size(Mel_IVec2 pos, Mel_IVec2 size)
{
    return (Mel_IRect){pos.x, pos.y, size.w, size.h};
}

static inline Mel_IVec2 mel_irect_pos(Mel_IRect r)
{
    return mel_ivec2(r.x, r.y);
}

static inline Mel_IVec2 mel_irect_size(Mel_IRect r)
{
    return mel_ivec2(r.w, r.h);
}

static inline i32 mel_irect_width(Mel_IRect r)
{
    return r.w;
}

static inline i32 mel_irect_height(Mel_IRect r)
{
    return r.h;
}

static inline bool mel_irect_contains_point(Mel_IRect r, Mel_IVec2 p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

static inline bool mel_irect_overlaps(Mel_IRect a, Mel_IRect b)
{
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

static inline Mel_IRect mel_irect_expand(Mel_IRect r, Mel_IVec2 amount)
{
    return (Mel_IRect){r.x - amount.x, r.y - amount.y, r.w + amount.x * 2, r.h + amount.y * 2};
}

static inline Mel_IRect mel_irect_translate(Mel_IRect r, Mel_IVec2 offset)
{
    return (Mel_IRect){r.x + offset.x, r.y + offset.y, r.w, r.h};
}

static inline Mel_IVec2 mel_irect_corner(Mel_IRect r, i32 index)
{
    switch (index) {
    case 0: return mel_ivec2(r.x, r.y);
    case 1: return mel_ivec2(r.x + r.w, r.y);
    case 2: return mel_ivec2(r.x + r.w, r.y + r.h);
    case 3: return mel_ivec2(r.x, r.y + r.h);
    default: return mel_ivec2(0, 0);
    }
}

static inline void mel_irect_corners(Mel_IRect r, Mel_IVec2 out[4])
{
    out[0] = mel_ivec2(r.x, r.y);
    out[1] = mel_ivec2(r.x + r.w, r.y);
    out[2] = mel_ivec2(r.x + r.w, r.y + r.h);
    out[3] = mel_ivec2(r.x, r.y + r.h);
}

static inline void mel_irect_add_point(Mel_IRect* r, Mel_IVec2 p)
{
    i32 xmin = r->x < p.x ? r->x : p.x;
    i32 ymin = r->y < p.y ? r->y : p.y;
    i32 xmax = (r->x + r->w) > p.x ? (r->x + r->w) : p.x;
    i32 ymax = (r->y + r->h) > p.y ? (r->y + r->h) : p.y;
    r->x = xmin;
    r->y = ymin;
    r->w = xmax - xmin;
    r->h = ymax - ymin;
}

static inline Mel_Rect mel_irect_to_rect(Mel_IRect r)
{
    return mel_rect((f32)r.x, (f32)r.y, (f32)r.w, (f32)r.h);
}

static inline Mel_IRect mel_rect_to_irect(Mel_Rect r)
{
    return mel_irect((i32)r.x, (i32)r.y, (i32)r.w, (i32)r.h);
}
