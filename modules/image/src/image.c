#include <image/image.h>

#include "format_internal.h"

#include <allocator/allocator.h>
#include <debug/assert.h>

#include <string.h>

usize mel_image_byte_size(const mel_image_format* f, i32 w, i32 h, u32 row_align)
{
    if (!f || w <= 0 || h <= 0)
        return 0;

    usize total = 0;
    for (i32 k = 0; k < f->plane_count; k++)
    {
        mel_image_plane_geom g = f->geom(f, w, h, k, row_align);
        usize                end = g.offset + (usize)g.stride * (usize)g.h;
        if (end > total)
            total = end;
    }
    return total;
}

static bool mel_image__alloc(Mel_Image* out, const mel_image_format* f, i32 w, i32 h, u32 row_align, const Mel_Alloc* a, bool zero)
{
    if (!out || !f || !a || w <= 0 || h <= 0)
        return false;
    if (row_align == 0)
        row_align = 1;
    if (row_align > 1 && (row_align & (row_align - 1)) != 0)
        return false;

    usize total = mel_image_byte_size(f, w, h, row_align);
    if (total == 0)
        return false;

    u8* data = (row_align > 1) ? (u8*)mel_aligned_alloc(a, total, row_align) : (u8*)mel_alloc(a, total);
    if (!data)
        return false;
    if (zero)
        memset(data, 0, total);

    out->format = f;
    out->w = w;
    out->h = h;
    out->alloc = a;
    out->data = data;
    out->row_align = row_align;
    out->wrapped = NULL;
    return true;
}

bool mel_image__init_uninit(Mel_Image* out, const mel_image_format* f, i32 w, i32 h, const Mel_Alloc* a) { return mel_image__alloc(out, f, w, h, 1, a, false); }

bool mel_image_init_aligned(Mel_Image* out, const mel_image_format* f, i32 w, i32 h, u32 row_align, const Mel_Alloc* a) { return mel_image__alloc(out, f, w, h, row_align, a, true); }

bool mel_image_init(Mel_Image* out, const mel_image_format* f, i32 w, i32 h, const Mel_Alloc* a) { return mel_image_init_aligned(out, f, w, h, 1, a); }

bool mel_image_wrap(Mel_Image* out, const mel_image_format* f, i32 w, i32 h, const Mel_Image_Plane* planes, i32 count)
{
    if (!out || !f || !planes || w <= 0 || h <= 0)
        return false;
    if (count != f->plane_count)
        return false;

    for (i32 k = 0; k < count; k++)
    {
        mel_image_plane_geom g = f->geom(f, w, h, k, 1);
        mel_assert(planes[k].pixels && "mel_image_wrap: plane pixels NULL");
        mel_assert(g.bpp > 0 && "mel_image_wrap: zero bpp");
        mel_assert(planes[k].stride >= g.w * g.bpp && "mel_image_wrap: plane stride < w*bpp (transposed plane?)");
        mel_assert(planes[k].h >= g.h && "mel_image_wrap: plane h < format-derived plane h");
    }

    out->format = f;
    out->w = w;
    out->h = h;
    out->alloc = NULL;
    out->data = NULL;
    out->row_align = 1;
    out->wrapped = planes;
    return true;
}

bool mel_image_wrap_plane(Mel_Image* out, const mel_image_format* f, const Mel_Image_Plane* plane)
{
    if (!out || !f || !plane || plane->w <= 0 || plane->h <= 0)
        return false;
    if (f->plane_count != 1 || f->planar)
        return false;
    return mel_image_wrap(out, f, plane->w, plane->h, plane, 1);
}

void mel_image_free(Mel_Image* img)
{
    if (!img)
        return;
    if (img->alloc && img->data)
    {
        if (img->row_align > 1)
            mel_aligned_dealloc(img->alloc, img->data, img->row_align);
        else
            mel_dealloc(img->alloc, img->data);
    }
    memset(img, 0, sizeof(*img));
}

i32 mel_image_plane_count(const Mel_Image* img) { return (img && img->format) ? img->format->plane_count : 0; }

Mel_Image_Plane mel_image_plane(const Mel_Image* img, i32 plane)
{
    mel_assert(img && img->format);
    mel_assert(plane >= 0 && plane < img->format->plane_count);

    mel_image_plane_geom g = img->format->geom(img->format, img->w, img->h, plane, img->row_align);

    if (img->wrapped)
    {
        Mel_Image_Plane p = img->wrapped[plane];
        p.bpp = g.bpp;
        return p;
    }

    Mel_Image_Plane p = { img->data + g.offset, g.stride, g.w, g.h, g.bpp };
    return p;
}
