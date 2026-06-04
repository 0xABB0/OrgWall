#pragma once

#include <core/types.h>

typedef struct mel_image_format mel_image_format;

extern const mel_image_format mel_image_rgba8;
extern const mel_image_format mel_image_rgba8_srgb;
extern const mel_image_format mel_image_rgba8_premul;
extern const mel_image_format mel_image_bgra8;
extern const mel_image_format mel_image_rgb8;
extern const mel_image_format mel_image_gray8;
extern const mel_image_format mel_image_gray16;
extern const mel_image_format mel_image_r8;
extern const mel_image_format mel_image_rg8;
extern const mel_image_format mel_image_rgba16f;
extern const mel_image_format mel_image_rgba32f;
extern const mel_image_format mel_image_nv12;
extern const mel_image_format mel_image_nv12_full;
extern const mel_image_format mel_image_nv21;
extern const mel_image_format mel_image_i420;
extern const mel_image_format mel_image_i422;
extern const mel_image_format mel_image_i444;
extern const mel_image_format mel_image_yuyv;
extern const mel_image_format mel_image_uyvy;

i32         mel_image_format_plane_count(const mel_image_format* f);
i32         mel_image_format_channels(const mel_image_format* f);
const char* mel_image_format_name(const mel_image_format* f);
bool        mel_image_format_has_luma(const mel_image_format* f);
