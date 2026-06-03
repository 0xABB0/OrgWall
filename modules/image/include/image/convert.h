#pragma once

#include <image/image.h>

mel_image_gray mel_image_gray_borrow(const Mel_Image* img);
bool           mel_image_to_gray(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out_gray8);
bool           mel_image_to_rgba(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out_rgba8);

bool mel_image_convert(const Mel_Image* src, Mel_Image* dst);
bool mel_image_convert_scratch(const Mel_Image* src, Mel_Image* dst, const Mel_Alloc* scratch);
bool mel_image_convert_via_canonical(const Mel_Image* src, Mel_Image* dst, const Mel_Alloc* scratch);
bool mel_image_convert_new(const Mel_Image* src, const mel_image_format* fmt, const Mel_Alloc* a, Mel_Image* out);

bool mel_image_premultiply(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out);
bool mel_image_unpremultiply(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out);
