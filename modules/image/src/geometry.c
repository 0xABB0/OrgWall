#include <image/geometry.h>

#include "format_internal.h"

#include <allocator/allocator.h>
#include <debug/assert.h>
#include <log/log.h>

#include <string.h>

typedef void (*mel_image_resample_u8)(const u8* src, i32 sstride, i32 sw, i32 sh, u8* dst, i32 dstride, i32 dw, i32 dh, i32 ch);

typedef struct
{
    const u8* base;
    i32       stride;
    i32       w, h;
    i32       ch;
    i32       bps;
    f32 (*load)(const u8*);
} mel_image_f32_src;

typedef struct
{
    u8* base;
    i32 stride;
    i32 w, h;
    i32 ch;
    i32 bps;
    void (*store)(u8*, f32);
} mel_image_f32_dst;

typedef void (*mel_image_resample_f32)(const mel_image_f32_src* s, const mel_image_f32_dst* d, f32* rowa, f32* rowb);

struct mel_image_filter
{
    const char*            name;
    bool                   area;
    i32                    scratch_rows;
    mel_image_resample_u8  resample_u8;
    mel_image_resample_f32 resample_f32;
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

static void resample_nearest_f32(const mel_image_f32_src* s, const mel_image_f32_dst* d, f32* rowa, f32* rowb)
{
    (void)rowa;
    (void)rowb;
    i32 ch = s->ch;
    i32 sbps = s->bps, dbps = d->bps;
    f32 (*load)(const u8*) = s->load;
    void (*store)(u8*, f32) = d->store;
    i64 step_x = ((i64)s->w << 16) / d->w;
    i64 step_y = ((i64)s->h << 16) / d->h;
    i64 acc_y = step_y >> 1;
    for (i32 y = 0; y < d->h; y++, acc_y += step_y)
    {
        i32 ny = (i32)(acc_y >> 16);
        if (ny >= s->h)
            ny = s->h - 1;
        const u8* restrict srow = s->base + (usize)ny * s->stride;
        u8* restrict drow = d->base + (usize)y * d->stride;
        i64 acc_x = step_x >> 1;
        for (i32 x = 0; x < d->w; x++, acc_x += step_x)
        {
            i32 nx = (i32)(acc_x >> 16);
            if (nx >= s->w)
                nx = s->w - 1;
            const u8* sp = srow + (usize)nx * sbps * ch;
            u8*       dp = drow + (usize)x * dbps * ch;
            for (i32 c = 0; c < ch; c++)
                store(dp + (usize)c * dbps, load(sp + (usize)c * sbps));
        }
    }
}

static void load_row_f32(const mel_image_f32_src* s, i32 ny, f32* row)
{
    const u8* srow = s->base + (usize)ny * s->stride;
    for (i32 x = 0; x < s->w; x++)
        for (i32 c = 0; c < s->ch; c++)
            row[(usize)x * s->ch + c] = s->load(srow + (usize)x * s->bps * s->ch + (usize)c * s->bps);
}

static void resample_bilinear_f32(const mel_image_f32_src* s, const mel_image_f32_dst* d, f32* rowa, f32* rowb)
{
    i32 ch = s->ch;
    i32 dbps = d->bps;
    void (*store)(u8*, f32) = d->store;
    f32 scale_x = (f32)s->w / (f32)d->w;
    f32 scale_y = (f32)s->h / (f32)d->h;
    i32 cached0 = -1, cached1 = -1;
    for (i32 y = 0; y < d->h; y++)
    {
        f32 syf = ((f32)y + 0.5f) * scale_y - 0.5f;
        if (syf < 0.0f)
            syf = 0.0f;
        i32 sy0 = (i32)syf;
        f32 wy = syf - (f32)sy0;
        i32 sy1 = sy0 + 1;
        if (sy1 >= s->h)
            sy1 = s->h - 1;
        if (sy0 >= s->h)
            sy0 = s->h - 1;

        if (sy0 != cached0)
        {
            load_row_f32(s, sy0, rowa);
            cached0 = sy0;
        }
        if (sy1 != cached1)
        {
            load_row_f32(s, sy1, rowb);
            cached1 = sy1;
        }

        u8* restrict drow = d->base + (usize)y * d->stride;
        for (i32 x = 0; x < d->w; x++)
        {
            f32 sxf = ((f32)x + 0.5f) * scale_x - 0.5f;
            if (sxf < 0.0f)
                sxf = 0.0f;
            i32 sx0 = (i32)sxf;
            f32 wx = sxf - (f32)sx0;
            i32 sx1 = sx0 + 1;
            if (sx1 >= s->w)
                sx1 = s->w - 1;
            if (sx0 >= s->w)
                sx0 = s->w - 1;

            u8* dp = drow + (usize)x * dbps * ch;
            for (i32 c = 0; c < ch; c++)
            {
                f32 a00 = rowa[(usize)sx0 * ch + c];
                f32 a01 = rowa[(usize)sx1 * ch + c];
                f32 a10 = rowb[(usize)sx0 * ch + c];
                f32 a11 = rowb[(usize)sx1 * ch + c];
                f32 top = a00 + (a01 - a00) * wx;
                f32 bot = a10 + (a11 - a10) * wx;
                store(dp + (usize)c * dbps, top + (bot - top) * wy);
            }
        }
    }
}

static void resample_box_f32(const mel_image_f32_src* s, const mel_image_f32_dst* d, f32* rowa, f32* rowb)
{
    (void)rowa;
    (void)rowb;
    i32 ch = s->ch;
    i32 sbps = s->bps, dbps = d->bps;
    i32 sstride = s->stride;
    i32 xstep = sbps * ch;
    f32 (*load)(const u8*) = s->load;
    void (*store)(u8*, f32) = d->store;
    for (i32 y = 0; y < d->h; y++)
    {
        i32 sy0 = (i32)(((i64)y * s->h) / d->h);
        i32 sy1 = (i32)(((i64)(y + 1) * s->h) / d->h);
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        if (sy1 > s->h)
            sy1 = s->h;
        u8* restrict drow = d->base + (usize)y * d->stride;
        for (i32 x = 0; x < d->w; x++)
        {
            i32 sx0 = (i32)(((i64)x * s->w) / d->w);
            i32 sx1 = (i32)(((i64)(x + 1) * s->w) / d->w);
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            if (sx1 > s->w)
                sx1 = s->w;

            f32 area = (f32)((sx1 - sx0) * (sy1 - sy0));
            u8* dp = drow + (usize)x * dbps * ch;
            for (i32 c = 0; c < ch; c++)
            {
                f32                sp_acc = 0.0f;
                const u8* restrict sp = s->base + (usize)sy0 * sstride + (usize)(sx0 * ch + c) * sbps;
                for (i32 yy = sy0; yy < sy1; yy++)
                {
                    const u8* q = sp;
                    for (i32 xx = sx0; xx < sx1; xx++)
                    {
                        sp_acc += load(q);
                        q += xstep;
                    }
                    sp += sstride;
                }
                store(dp + (usize)c * dbps, sp_acc / area);
            }
        }
    }
}

const mel_image_filter mel_image_filter_nearest = { "nearest", false, 0, resample_nearest_u8, resample_nearest_f32 };
const mel_image_filter mel_image_filter_bilinear = { "bilinear", false, 2, resample_bilinear_u8, resample_bilinear_f32 };
const mel_image_filter mel_image_filter_box = { "box", true, 0, resample_box_u8, resample_box_f32 };

static bool mel_image__is_u8_packed(const mel_image_format* f) { return f && !f->planar && f->bytes_per_sample == 1 && f->channels > 0 && f->bytes_per_pixel == f->channels; }

static bool mel_image__resize_packed_f32(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter, const Mel_Alloc* a)
{
    const mel_image_format* f = src->format;
    if (!filter->resample_f32)
    {
        mel_log_error("image", "resize: filter %s has no f32 kernel", filter->name);
        return false;
    }

    Mel_Image_Plane sp = mel_image_plane(src, 0);
    Mel_Image_Plane dp = mel_image_plane(dst, 0);

    f32* rowa = NULL;
    f32* rowb = NULL;
    if (filter->scratch_rows > 0)
    {
        if (!a)
        {
            mel_log_error("image", "resize: filter %s on %s needs scratch but no allocator available (pass mel_image_resize_scratch)", filter->name, f->name);
            return false;
        }
        usize row_elems = (usize)src->w * (usize)f->channels;
        usize total = row_elems * (usize)filter->scratch_rows;
        rowa = (f32*)mel_alloc(a, total * sizeof(f32));
        if (!rowa)
        {
            mel_log_error("image", "resize: scratch OOM (%zu f32)", total);
            return false;
        }
        rowb = rowa + row_elems;
    }

    mel_image_f32_src s = { sp.pixels, sp.stride, src->w, src->h, f->channels, f->bytes_per_sample, f->sample_load };
    mel_image_f32_dst d = { dp.pixels, dp.stride, dst->w, dst->h, f->channels, f->bytes_per_sample, f->sample_store };
    filter->resample_f32(&s, &d, rowa, rowb);

    if (rowa)
        mel_dealloc(a, rowa);
    return true;
}

static bool mel_image__resize_planar(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter)
{
    const mel_image_format* f = src->format;
    if (!filter->resample_u8)
    {
        mel_log_error("image", "resize: filter %s has no u8 kernel", filter->name);
        return false;
    }

    for (i32 k = 0; k < f->plane_count; k++)
    {
        Mel_Image_Plane sp = mel_image_plane(src, k);
        Mel_Image_Plane dp = mel_image_plane(dst, k);
        if (sp.bpp != 1 && sp.bpp != 2)
        {
            mel_log_error("image", "resize: planar %s plane %d bpp %d unsupported", f->name, k, sp.bpp);
            return false;
        }
        filter->resample_u8(sp.pixels, sp.stride, sp.w, sp.h, dp.pixels, dp.stride, dp.w, dp.h, sp.bpp);
    }
    return true;
}

static bool mel_image__resize(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter, const Mel_Alloc* scratch)
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

    if (mel_image__is_u8_packed(f))
    {
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

    if (f->planar)
        return mel_image__resize_planar(src, dst, filter);

    if (!f->planar && f->sample_load && f->sample_store && f->channels > 0 && f->bytes_per_pixel == f->channels * f->bytes_per_sample)
    {
        const Mel_Alloc* a = scratch ? scratch : (dst->alloc ? dst->alloc : src->alloc);
        return mel_image__resize_packed_f32(src, dst, filter, a);
    }

    mel_log_error("image", "resize: no path for format %s", f->name);
    return false;
}

bool mel_image_resize(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter) { return mel_image__resize(src, dst, filter, NULL); }

bool mel_image_resize_scratch(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter, const Mel_Alloc* scratch) { return mel_image__resize(src, dst, filter, scratch); }

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

static bool mel_image__roi_planes(const Mel_Image* img, i32 rx, i32 ry, i32 w, i32 h, Mel_Image_Plane* out)
{
    const mel_image_format* f = img->format;
    for (i32 k = 0; k < f->plane_count; k++)
    {
        mel_image_plane_geom g = f->geom(f, img->w, img->h, k, 1);
        i32                  ssx = (g.w < img->w) ? 1 : 0;
        i32                  ssy = (g.h < img->h) ? 1 : 0;
        if (((rx | w) & ssx) || ((ry | h) & ssy))
        {
            mel_log_error("image", "blit: planar %s requires chroma-aligned rect", f->name);
            return false;
        }
        Mel_Image_Plane p = mel_image_plane(img, k);
        i32             px = ssx ? rx >> 1 : rx;
        i32             py = ssy ? ry >> 1 : ry;
        i32             pw = ssx ? w >> 1 : w;
        i32             ph = ssy ? h >> 1 : h;
        out[k].pixels = p.pixels + (usize)py * p.stride + (usize)px * p.bpp;
        out[k].stride = p.stride;
        out[k].w = pw;
        out[k].h = ph;
        out[k].bpp = p.bpp;
    }
    return true;
}

static bool mel_image__blit_planar_cross(Mel_Image* dst, i32 dx, i32 dy, const Mel_Image* src, i32 sx, i32 sy, i32 w, i32 h, const Mel_Alloc* a)
{
    i32              count = src->format->plane_count > dst->format->plane_count ? src->format->plane_count : dst->format->plane_count;
    Mel_Image_Plane* planes = (Mel_Image_Plane*)mel_alloc(a, (usize)count * 2 * sizeof(Mel_Image_Plane));
    if (!planes)
    {
        mel_log_error("image", "blit: plane scratch OOM");
        return false;
    }
    Mel_Image_Plane* sp = planes;
    Mel_Image_Plane* dp = planes + count;

    bool ok = mel_image__roi_planes(src, sx, sy, w, h, sp) && mel_image__roi_planes(dst, dx, dy, w, h, dp);
    if (ok)
    {
        Mel_Image ssub, dsub;
        ok = mel_image_wrap(&ssub, src->format, w, h, sp, src->format->plane_count) && mel_image_wrap(&dsub, dst->format, w, h, dp, dst->format->plane_count) && mel_image_convert_scratch(&ssub, &dsub, a);
    }

    mel_dealloc(a, planes);
    return ok;
}

#ifndef NDEBUG
static void mel_image__blit_assert_no_overlap(const Mel_Image* dst, i32 dx, i32 dy, const Mel_Image* src, i32 sx, i32 sy, i32 w, i32 h)
{
    i32 planes = src->format->plane_count < dst->format->plane_count ? src->format->plane_count : dst->format->plane_count;
    for (i32 k = 0; k < planes; k++)
    {
        Mel_Image_Plane s = mel_image_plane(src, k);
        Mel_Image_Plane d = mel_image_plane(dst, k);
        const u8*       s0 = s.pixels + (usize)sy * s.stride;
        const u8*       s1 = s.pixels + (usize)(sy + h - 1) * s.stride + (usize)(sx + w) * s.bpp;
        const u8*       d0 = d.pixels + (usize)dy * d.stride;
        const u8*       d1 = d.pixels + (usize)(dy + h - 1) * d.stride + (usize)(dx + w) * d.bpp;
        mel_assert(!(s0 < d1 && d0 < s1) && "mel_image_blit: src and dst regions must not alias");
    }
}
#endif

bool mel_image_blit(Mel_Image* dst, i32 dx, i32 dy, const Mel_Image* src, i32 sx, i32 sy, i32 w, i32 h)
{
    if (!dst || !src || !dst->format || !src->format)
        return false;

    if (!mel_image__clip_rect(&dx, &dy, &sx, &sy, &w, &h, dst->w, dst->h, src->w, src->h))
        return false;

#ifndef NDEBUG
    mel_image__blit_assert_no_overlap(dst, dx, dy, src, sx, sy, w, h);
#endif

    if (src->format == dst->format)
        return mel_image__blit_same(dst, dx, dy, src, sx, sy, w, h);

    const Mel_Alloc* a = dst->alloc ? dst->alloc : src->alloc;
    if (!a)
    {
        mel_log_error("image", "blit: %s -> %s needs scratch but both images are non-owning", src->format->name, dst->format->name);
        return false;
    }

    if (src->format->planar || dst->format->planar)
        return mel_image__blit_planar_cross(dst, dx, dy, src, sx, sy, w, h, a);

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

static void mel_image__orient_plane(Mel_Image_Plane s, Mel_Image_Plane d, i32 eb, i32 q, bool flip_x)
{
    i32   sw = s.w, sh = s.h;
    isize dstride = d.stride;
    isize cb = eb;

    isize fx0 = flip_x ? sw - 1 : 0;
    isize fs = flip_x ? -1 : 1;

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
            for (i32 c = 0; c < eb; c++)
                dp[c] = sp[c];
            sp += eb;
            dp += dstep_x;
        }
    }
}

static bool mel_image__orient_validate(const mel_image_format* f, const Mel_Image* src, bool swap)
{
    if (!swap || !f->planar)
        return true;
    for (i32 k = 0; k < f->plane_count; k++)
    {
        mel_image_plane_geom g = f->geom(f, src->w, src->h, k, 1);
        i32                  ssx = (g.w < src->w) ? 1 : 0;
        i32                  ssy = (g.h < src->h) ? 1 : 0;
        if (ssx != ssy)
        {
            mel_log_error("image", "orient: %s odd quarter-turn needs symmetric subsampling (plane %d)", f->name, k);
            return false;
        }
    }
    return true;
}

bool mel_image_orient(const Mel_Image* src, Mel_Image* dst, Mel_Image_Orient o)
{
    if (!src || !dst || !src->format || !dst->format)
        return false;
    if (src->format != dst->format)
    {
        mel_log_error("image", "orient: format mismatch %s -> %s", src->format->name, dst->format->name);
        return false;
    }

    const mel_image_format* f = src->format;
    i32                     q = ((o.quarter_turns % 4) + 4) % 4;
    bool                    swap = (q == 1 || q == 3);
    i32                     ow = swap ? src->h : src->w;
    i32                     oh = swap ? src->w : src->h;

    if (dst->w != ow || dst->h != oh)
    {
        mel_log_error("image", "orient: dst %dx%d does not match oriented extent %dx%d", dst->w, dst->h, ow, oh);
        return false;
    }

    if (mel_image__is_u8_packed(f))
    {
        mel_image__orient_plane(mel_image_plane(src, 0), mel_image_plane(dst, 0), f->channels, q, o.flip_x);
        return true;
    }

    if (f->planar)
    {
        if (!mel_image__orient_validate(f, src, swap))
            return false;

        for (i32 k = 0; k < f->plane_count; k++)
        {
            mel_image_plane_geom g = f->geom(f, src->w, src->h, k, 1);
            if (g.bpp != 1 && g.bpp != 2)
            {
                mel_log_error("image", "orient: planar %s plane %d bpp %d unsupported", f->name, k, g.bpp);
                return false;
            }
            mel_image__orient_plane(mel_image_plane(src, k), mel_image_plane(dst, k), g.bpp, q, o.flip_x);
        }
        return true;
    }

    mel_log_error("image", "orient: no path for format %s", f->name);
    return false;
}

bool mel_image_orient_new(const Mel_Image* src, const Mel_Alloc* a, Mel_Image_Orient o, Mel_Image* out)
{
    if (!src || !src->format || !a || !out)
        return false;

    const mel_image_format* f = src->format;
    i32                     q = ((o.quarter_turns % 4) + 4) % 4;
    bool                    swap = (q == 1 || q == 3);
    i32                     ow = swap ? src->h : src->w;
    i32                     oh = swap ? src->w : src->h;

    if (!mel_image__is_u8_packed(f) && !f->planar)
    {
        mel_log_error("image", "orient: no path for format %s", f->name);
        return false;
    }
    if (!mel_image__orient_validate(f, src, swap))
        return false;

    if (!mel_image__init_uninit(out, f, ow, oh, a))
        return false;
    if (!mel_image_orient(src, out, o))
    {
        mel_image_free(out);
        return false;
    }
    return true;
}
