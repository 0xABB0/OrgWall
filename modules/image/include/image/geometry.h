#pragma once

#include <core/types.h>

#include <debug/assert.h>

#include <image/image.h>

typedef struct mel_image_filter mel_image_filter;

extern const mel_image_filter mel_image_filter_nearest;
extern const mel_image_filter mel_image_filter_bilinear;
extern const mel_image_filter mel_image_filter_box;

const char* mel_image_filter_name(const mel_image_filter* f);

typedef struct
{
    i32  quarter_turns;
    bool flip_x;
} Mel_Image_Orient;

static inline Mel_Image_Plane mel_image_plane_roi(Mel_Image_Plane p, i32 x, i32 y, i32 w, i32 h)
{
    mel_assert(p.bpp > 0);
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x > p.w)
        x = p.w;
    if (y > p.h)
        y = p.h;
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    if (x + w > p.w)
        w = p.w - x;
    if (y + h > p.h)
        h = p.h - y;

    Mel_Image_Plane r;
    r.pixels = p.pixels + (usize)y * (usize)p.stride + (usize)x * (usize)p.bpp;
    r.stride = p.stride;
    r.w = w;
    r.h = h;
    r.bpp = p.bpp;
    return r;
}

static inline mel_image_gray mel_image_gray_roi(mel_image_gray v, i32 x, i32 y, i32 w, i32 h)
{
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x > v.w)
        x = v.w;
    if (y > v.h)
        y = v.h;
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    if (x + w > v.w)
        w = v.w - x;
    if (y + h > v.h)
        h = v.h - y;

    mel_image_gray r;
    r.pixels = v.pixels + (usize)y * (usize)v.stride + (usize)x;
    r.stride = v.stride;
    r.w = w;
    r.h = h;
    return r;
}

bool mel_image_blit(Mel_Image* dst, i32 dx, i32 dy, const Mel_Image* src, i32 sx, i32 sy, i32 w, i32 h);
bool mel_image_resize(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter);
bool mel_image_resize_scratch(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter, const Mel_Alloc* scratch);
bool mel_image_resize_new(const Mel_Image* src, i32 w, i32 h, const mel_image_filter* filter, const Mel_Alloc* a, Mel_Image* out);
bool mel_image_orient(const Mel_Image* src, Mel_Image* dst, Mel_Image_Orient o);
bool mel_image_orient_new(const Mel_Image* src, const Mel_Alloc* a, Mel_Image_Orient o, Mel_Image* out);
