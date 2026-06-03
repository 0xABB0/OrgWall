#include <image/convert.h>

#include "format_internal.h"

#include <allocator/allocator.h>
#include <debug/assert.h>
#include <log/log.h>

#include <string.h>

mel_image_gray mel_image_gray_borrow(const Mel_Image* img)
{
    mel_assert(img && img->format);
    mel_assert(img->format->has_luma);

    Mel_Image_Plane p = mel_image_plane(img, 0);
    mel_image_gray  g = { (const u8*)p.pixels, p.stride, p.w, p.h };
    return g;
}

bool mel_image_to_gray(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out)
{
    if (!src || !src->format || !a || !out)
        return false;

    i32 w = src->w;
    i32 h = src->h;
    if (!mel_image_init(out, &mel_image_gray8, w, h, a))
        return false;

    Mel_Image_Plane         dst = mel_image_plane(out, 0);
    const mel_image_format* f = src->format;

    if (f->has_luma)
    {
        Mel_Image_Plane y = mel_image_plane(src, 0);
        for (i32 r = 0; r < h; r++)
            memcpy(dst.pixels + (usize)r * dst.stride, y.pixels + (usize)r * y.stride, (usize)w);
        return true;
    }

    i32 bpc = f->channels ? f->bytes_per_pixel / f->channels : 0;
    if (!f->planar && bpc == 1 && f->off_r >= 0 && f->off_g >= 0 && f->off_b >= 0)
    {
        Mel_Image_Plane s = mel_image_plane(src, 0);
        for (i32 r = 0; r < h; r++)
        {
            const u8* srow = s.pixels + (usize)r * s.stride;
            u8*       drow = dst.pixels + (usize)r * dst.stride;
            for (i32 x = 0; x < w; x++)
            {
                const u8* px = srow + (usize)x * f->bytes_per_pixel;
                u32       R = px[f->off_r];
                u32       G = px[f->off_g];
                u32       B = px[f->off_b];
                drow[x] = (u8)((R * 77 + G * 150 + B * 29) >> 8);
            }
        }
        return true;
    }

    mel_log_error("image", "to_gray: no path for format %s", f->name);
    mel_image_free(out);
    return false;
}
