#include <image/geometry.h>

#include "format_internal.h"

#include <allocator/allocator.h>
#include <debug/assert.h>
#include <log/log.h>

#include <string.h>

typedef void (*mel_image_resample_u8)(const u8* src, i32 sstride, i32 sw, i32 sh, u8* dst, i32 dstride, i32 dw, i32 dh, i32 ch);

struct mel_image_filter
{
    const char*           name;
    bool                  area;
    mel_image_resample_u8 resample_u8;
};

const char* mel_image_filter_name(const mel_image_filter* f) { return f ? f->name : ""; }

static void resample_nearest_u8(const u8* src, i32 sstride, i32 sw, i32 sh, u8* dst, i32 dstride, i32 dw, i32 dh, i32 ch)
{
    i64 step_x = ((i64)sw << 16) / dw;
    i64 step_y = ((i64)sh << 16) / dh;
    i64 acc_y = step_y >> 1;
    for (i32 y = 0; y < dh; y++, acc_y += step_y)
    {
        i32 ny = (i32)(acc_y >> 16);
        if (ny >= sh)
            ny = sh - 1;
        const u8* srow = src + (usize)ny * sstride;
        u8*       drow = dst + (usize)y * dstride;
        i64       acc_x = step_x >> 1;
        for (i32 x = 0; x < dw; x++, acc_x += step_x)
        {
            i32 nx = (i32)(acc_x >> 16);
            if (nx >= sw)
                nx = sw - 1;
            const u8* sp = srow + (usize)nx * ch;
            u8*       dp = drow + (usize)x * ch;
            for (i32 c = 0; c < ch; c++)
                dp[c] = sp[c];
        }
    }
}

static void resample_bilinear_u8(const u8* src, i32 sstride, i32 sw, i32 sh, u8* dst, i32 dstride, i32 dw, i32 dh, i32 ch)
{
    i64 scale_x = ((i64)sw << 16) / dw;
    i64 scale_y = ((i64)sh << 16) / dh;
    for (i32 y = 0; y < dh; y++)
    {
        i64 syf = ((i64)y * scale_y) + (scale_y >> 1) - (1 << 15);
        if (syf < 0)
            syf = 0;
        i32 sy0 = (i32)(syf >> 16);
        i32 wy = (i32)(syf & 0xFFFF);
        i32 sy1 = sy0 + 1;
        if (sy1 >= sh)
            sy1 = sh - 1;
        if (sy0 >= sh)
            sy0 = sh - 1;
        const u8* row0 = src + (usize)sy0 * sstride;
        const u8* row1 = src + (usize)sy1 * sstride;
        u8*       drow = dst + (usize)y * dstride;
        for (i32 x = 0; x < dw; x++)
        {
            i64 sxf = ((i64)x * scale_x) + (scale_x >> 1) - (1 << 15);
            if (sxf < 0)
                sxf = 0;
            i32 sx0 = (i32)(sxf >> 16);
            i32 wx = (i32)(sxf & 0xFFFF);
            i32 sx1 = sx0 + 1;
            if (sx1 >= sw)
                sx1 = sw - 1;
            if (sx0 >= sw)
                sx0 = sw - 1;

            const u8* p00 = row0 + (usize)sx0 * ch;
            const u8* p01 = row0 + (usize)sx1 * ch;
            const u8* p10 = row1 + (usize)sx0 * ch;
            const u8* p11 = row1 + (usize)sx1 * ch;
            u8*       dp = drow + (usize)x * ch;

            i32 iwx = 65536 - wx;
            i32 iwy = 65536 - wy;
            for (i32 c = 0; c < ch; c++)
            {
                i64 top = (i64)p00[c] * iwx + (i64)p01[c] * wx;
                i64 bot = (i64)p10[c] * iwx + (i64)p11[c] * wx;
                i64 v = ((top >> 16) * iwy + (bot >> 16) * wy + (1 << 15)) >> 16;
                dp[c] = (u8)(v > 255 ? 255 : v);
            }
        }
    }
}

static void resample_box_u8(const u8* src, i32 sstride, i32 sw, i32 sh, u8* dst, i32 dstride, i32 dw, i32 dh, i32 ch)
{
    for (i32 y = 0; y < dh; y++)
    {
        i32 sy0 = (i32)(((i64)y * sh) / dh);
        i32 sy1 = (i32)(((i64)(y + 1) * sh) / dh);
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        if (sy1 > sh)
            sy1 = sh;
        u8* drow = dst + (usize)y * dstride;
        for (i32 x = 0; x < dw; x++)
        {
            i32 sx0 = (i32)(((i64)x * sw) / dw);
            i32 sx1 = (i32)(((i64)(x + 1) * sw) / dw);
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            if (sx1 > sw)
                sx1 = sw;

            u32 area = (u32)(sx1 - sx0) * (u32)(sy1 - sy0);
            u8* dp = drow + (usize)x * ch;
            for (i32 c = 0; c < ch; c++)
            {
                u32       acc = 0;
                const u8* sp = src + (usize)sy0 * sstride + (usize)sx0 * ch + c;
                for (i32 yy = sy0; yy < sy1; yy++)
                {
                    const u8* q = sp;
                    for (i32 xx = sx0; xx < sx1; xx++)
                    {
                        acc += *q;
                        q += ch;
                    }
                    sp += sstride;
                }
                dp[c] = (u8)((acc + area / 2) / area);
            }
        }
    }
}

const mel_image_filter mel_image_filter_nearest = { "nearest", false, resample_nearest_u8 };
const mel_image_filter mel_image_filter_bilinear = { "bilinear", false, resample_bilinear_u8 };
const mel_image_filter mel_image_filter_box = { "box", true, resample_box_u8 };

static bool mel_image__is_u8_packed(const mel_image_format* f)
{
    return f && !f->planar && f->bytes_per_sample == 1 && f->channels > 0 && f->bytes_per_pixel == f->channels;
}

bool mel_image_resize(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter)
{
    if (!src || !dst || !src->format || !dst->format || !filter)
        return false;
    if (src->format != dst->format)
    {
        mel_log_error("image", "resize: format mismatch %s -> %s (convert first)", src->format->name, dst->format->name);
        return false;
    }
    if (src->w <= 0 || src->h <= 0 || dst->w <= 0 || dst->h <= 0)
        return false;

    const mel_image_format* f = src->format;
    if (!mel_image__is_u8_packed(f))
    {
        mel_log_error("image", "resize: no path for format %s", f->name);
        return false;
    }
    if (!filter->resample_u8)
    {
        mel_log_error("image", "resize: filter %s has no u8 kernel", filter->name);
        return false;
    }

    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(dst, 0);
    filter->resample_u8(s.pixels, s.stride, src->w, src->h, d.pixels, d.stride, dst->w, dst->h, f->channels);
    return true;
}

bool mel_image_resize_new(const Mel_Image* src, i32 w, i32 h, const mel_image_filter* filter, const Mel_Alloc* a, Mel_Image* out)
{
    if (!src || !src->format || !filter || !a || !out || w <= 0 || h <= 0)
        return false;
    if (!mel_image__init_uninit(out, src->format, w, h, a))
        return false;
    if (!mel_image_resize(src, out, filter))
    {
        mel_image_free(out);
        return false;
    }
    return true;
}

static bool mel_image__clip_rect(i32* dx, i32* dy, i32* sx, i32* sy, i32* w, i32* h, i32 dw, i32 dh, i32 sw, i32 sh)
{
    if (*w <= 0 || *h <= 0)
        return false;

    if (*dx < 0)
    {
        *w += *dx;
        *sx -= *dx;
        *dx = 0;
    }
    if (*dy < 0)
    {
        *h += *dy;
        *sy -= *dy;
        *dy = 0;
    }
    if (*sx < 0)
    {
        *w += *sx;
        *dx -= *sx;
        *sx = 0;
    }
    if (*sy < 0)
    {
        *h += *sy;
        *dy -= *sy;
        *sy = 0;
    }
    if (*dx + *w > dw)
        *w = dw - *dx;
    if (*dy + *h > dh)
        *h = dh - *dy;
    if (*sx + *w > sw)
        *w = sw - *sx;
    if (*sy + *h > sh)
        *h = sh - *sy;

    return *w > 0 && *h > 0;
}

static bool mel_image__blit_same(Mel_Image* dst, i32 dx, i32 dy, const Mel_Image* src, i32 sx, i32 sy, i32 w, i32 h)
{
    const mel_image_format* f = src->format;

    if (!f->planar)
    {
        Mel_Image_Plane sp = mel_image_plane(src, 0);
        Mel_Image_Plane dp = mel_image_plane(dst, 0);
        usize           bytes = (usize)w * (usize)sp.bpp;
        for (i32 r = 0; r < h; r++)
        {
            const u8* srow = sp.pixels + (usize)(sy + r) * sp.stride + (usize)sx * sp.bpp;
            u8*       drow = dp.pixels + (usize)(dy + r) * dp.stride + (usize)dx * dp.bpp;
            memcpy(drow, srow, bytes);
        }
        return true;
    }

    for (i32 k = 0; k < f->plane_count; k++)
    {
        mel_image_plane_geom sg = f->geom(f, src->w, src->h, k, 1);
        i32                  ssx = (sg.w < src->w) ? 1 : 0;
        i32                  ssy = (sg.h < src->h) ? 1 : 0;
        if (((sx | dx | w) & ssx) || ((sy | dy | h) & ssy))
        {
            mel_log_error("image", "blit: planar %s requires chroma-aligned rect", f->name);
            return false;
        }

        Mel_Image_Plane sp = mel_image_plane(src, k);
        Mel_Image_Plane dp = mel_image_plane(dst, k);
        i32             psx = ssx ? sx >> 1 : sx;
        i32             psy = ssy ? sy >> 1 : sy;
        i32             pdx = ssx ? dx >> 1 : dx;
        i32             pdy = ssy ? dy >> 1 : dy;
        i32             pw = ssx ? w >> 1 : w;
        i32             ph = ssy ? h >> 1 : h;
        usize           bytes = (usize)pw * (usize)sp.bpp;

        for (i32 r = 0; r < ph; r++)
        {
            const u8* srow = sp.pixels + (usize)(psy + r) * sp.stride + (usize)psx * sp.bpp;
            u8*       drow = dp.pixels + (usize)(pdy + r) * dp.stride + (usize)pdx * dp.bpp;
            memcpy(drow, srow, bytes);
        }
    }
    return true;
}

bool mel_image_blit(Mel_Image* dst, i32 dx, i32 dy, const Mel_Image* src, i32 sx, i32 sy, i32 w, i32 h)
{
    if (!dst || !src || !dst->format || !src->format)
        return false;

    if (!mel_image__clip_rect(&dx, &dy, &sx, &sy, &w, &h, dst->w, dst->h, src->w, src->h))
        return false;

    if (src->format == dst->format)
        return mel_image__blit_same(dst, dx, dy, src, sx, sy, w, h);

    if (src->format->planar || dst->format->planar)
    {
        mel_log_error("image", "blit: cross-format planar %s -> %s unsupported", src->format->name, dst->format->name);
        return false;
    }

    const Mel_Alloc* a = dst->alloc ? dst->alloc : src->alloc;
    if (!a)
    {
        mel_log_error("image", "blit: %s -> %s needs scratch but both images are non-owning", src->format->name, dst->format->name);
        return false;
    }

    Mel_Image_Plane sp = mel_image_plane(src, 0);
    Mel_Image_Plane dp = mel_image_plane(dst, 0);

    Mel_Image_Plane srow_plane = { sp.pixels + (usize)sy * sp.stride + (usize)sx * sp.bpp, sp.stride, w, 1, sp.bpp };
    Mel_Image_Plane drow_plane = { dp.pixels + (usize)dy * dp.stride + (usize)dx * dp.bpp, dp.stride, w, 1, dp.bpp };

    Mel_Image srow_img, drow_img;
    if (!mel_image_wrap(&srow_img, src->format, w, 1, &srow_plane, 1))
        return false;
    if (!mel_image_wrap(&drow_img, dst->format, w, 1, &drow_plane, 1))
        return false;

    mel_image_kernel k = mel_image__find_kernel(src->format, dst->format);
    if (k)
    {
        for (i32 r = 0; r < h; r++)
        {
            srow_plane.pixels = sp.pixels + (usize)(sy + r) * sp.stride + (usize)sx * sp.bpp;
            drow_plane.pixels = dp.pixels + (usize)(dy + r) * dp.stride + (usize)dx * dp.bpp;
            k(&srow_img, &drow_img);
        }
        return true;
    }

    if (!src->format->to_canonical || !dst->format->from_canonical)
    {
        mel_log_error("image", "blit: no path %s -> %s", src->format->name, dst->format->name);
        return false;
    }

    mel_color* canon_row = (mel_color*)mel_alloc(a, (usize)w * sizeof(mel_color));
    if (!canon_row)
    {
        mel_log_error("image", "blit: scratch row OOM (%d px)", w);
        return false;
    }
    mel_image_canon canon = { canon_row, w };

    for (i32 r = 0; r < h; r++)
    {
        srow_plane.pixels = sp.pixels + (usize)(sy + r) * sp.stride + (usize)sx * sp.bpp;
        drow_plane.pixels = dp.pixels + (usize)(dy + r) * dp.stride + (usize)dx * dp.bpp;
        src->format->to_canonical(src->format, &srow_img, 0, canon);
        dst->format->from_canonical(dst->format, &drow_img, 0, canon);
    }

    mel_dealloc(a, canon_row);
    return true;
}

bool mel_image_orient(const Mel_Image* src, const Mel_Alloc* a, Mel_Image_Orient o, Mel_Image* out)
{
    if (!src || !src->format || !a || !out)
        return false;

    const mel_image_format* f = src->format;
    if (!mel_image__is_u8_packed(f))
    {
        mel_log_error("image", "orient: no path for format %s", f->name);
        return false;
    }

    i32  q = ((o.quarter_turns % 4) + 4) % 4;
    bool swap = (q == 1 || q == 3);
    i32  ow = swap ? src->h : src->w;
    i32  oh = swap ? src->w : src->h;

    if (!mel_image__init_uninit(out, f, ow, oh, a))
        return false;

    Mel_Image_Plane s = mel_image_plane(src, 0);
    Mel_Image_Plane d = mel_image_plane(out, 0);
    i32             ch = f->channels;
    i32             sw = src->w, sh = src->h;
    isize           dstride = d.stride;
    isize           cb = ch;

    isize fx0 = o.flip_x ? sw - 1 : 0;
    isize fs = o.flip_x ? -1 : 1;

    isize dx0, dy0, dstep_x, dstep_y;
    switch (q)
    {
    case 0:
        dx0 = fx0;
        dy0 = 0;
        dstep_x = fs * cb;
        dstep_y = dstride;
        break;
    case 1:
        dx0 = sh - 1;
        dy0 = fx0;
        dstep_x = fs * dstride;
        dstep_y = -cb;
        break;
    case 2:
        dx0 = sw - 1 - fx0;
        dy0 = sh - 1;
        dstep_x = -fs * cb;
        dstep_y = -dstride;
        break;
    default:
        dx0 = 0;
        dy0 = sw - 1 - fx0;
        dstep_x = -fs * dstride;
        dstep_y = cb;
        break;
    }

    u8* dbase = d.pixels + dy0 * dstride + dx0 * cb;

    for (i32 syp = 0; syp < sh; syp++)
    {
        const u8* sp = s.pixels + (usize)syp * s.stride;
        u8*       dp = dbase + (isize)syp * dstep_y;
        for (i32 sxp = 0; sxp < sw; sxp++)
        {
            for (i32 c = 0; c < ch; c++)
                dp[c] = sp[c];
            sp += ch;
            dp += dstep_x;
        }
    }
    return true;
}
