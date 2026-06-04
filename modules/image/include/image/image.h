#pragma once

#include <core/types.h>

#include <allocator/allocator.fwd.h>

#include <image/format.h>

typedef struct
{
    u8* pixels;
    i32 stride;
    i32 w, h;
    i32 bpp;
} Mel_Image_Plane;

typedef struct
{
    const u8* pixels;
    i32       stride;
    i32       w, h;
} mel_image_gray;

typedef struct
{
    const mel_image_format* format;
    i32                     w, h;
    const Mel_Alloc*        alloc;
    u8*                     data;
    u32                     row_align;
    const Mel_Image_Plane*  wrapped;
} Mel_Image;

bool mel_image_init(Mel_Image* out, const mel_image_format* fmt, i32 w, i32 h, const Mel_Alloc* a);
bool mel_image_init_aligned(Mel_Image* out, const mel_image_format* fmt, i32 w, i32 h, u32 row_align, const Mel_Alloc* a);
bool mel_image_wrap(Mel_Image* out, const mel_image_format* fmt, i32 w, i32 h, const Mel_Image_Plane* planes, i32 count);
bool mel_image_wrap_plane(Mel_Image* out, const mel_image_format* fmt, const Mel_Image_Plane* plane);
void mel_image_free(Mel_Image* img);

i32             mel_image_plane_count(const Mel_Image* img);
Mel_Image_Plane mel_image_plane(const Mel_Image* img, i32 plane);
usize           mel_image_byte_size(const mel_image_format* fmt, i32 w, i32 h, u32 row_align);

#include <image/convert.h>
#include <image/geometry.h>
#include <image/codec.h>
