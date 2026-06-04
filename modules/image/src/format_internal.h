#pragma once

#include <core/types.h>

#include <image/image.h>

#include <color/rgba.h>

typedef struct mel_image_plane_geom
{
    usize offset;
    i32   stride;
    i32   w, h;
    i32   bpp;
} mel_image_plane_geom;

typedef struct mel_image_canon
{
    mel_color* row;
    i32        w;
} mel_image_canon;

typedef struct mel_image_yuv
{
    float kr, kg, kb;
    bool  full_range;
    i8    u_byte, v_byte;
    bool  packed;
    i8    y0_byte, y1_byte, pu_byte, pv_byte;
} mel_image_yuv;

struct mel_image_format
{
    const char* name;
    i32         plane_count;
    i32         channels;
    i32         bytes_per_pixel;
    i8          off_r, off_g, off_b, off_a;
    bool        has_luma;
    bool        planar;
    bool        premultiplied;
    mel_image_plane_geom (*geom)(const mel_image_format* f, i32 w, i32 h, i32 plane, u32 row_align);

    i32 bytes_per_sample;
    f32 (*sample_load)(const u8* p);
    void (*sample_store)(u8* p, f32 v);
    float (*to_linear)(float c);
    float (*to_encoded)(float c);
    mel_image_yuv yuv;

    void (*to_canonical)(const mel_image_format* f, const Mel_Image* src, i32 y, mel_image_canon out);
    void (*from_canonical)(const mel_image_format* f, Mel_Image* dst, i32 y, mel_image_canon in);
};

static inline usize mel_image__align_up(usize x, u32 a) { return (a <= 1) ? x : (((x + a - 1) / a) * a); }

f32  mel_image__load_unorm8(const u8* p);
void mel_image__store_unorm8(u8* p, f32 v);
f32  mel_image__load_unorm16(const u8* p);
void mel_image__store_unorm16(u8* p, f32 v);
f32  mel_image__load_f16(const u8* p);
void mel_image__store_f16(u8* p, f32 v);
f32  mel_image__load_f32(const u8* p);
void mel_image__store_f32(u8* p, f32 v);

float mel_image__tf_linear(float c);

bool mel_image__init_uninit(Mel_Image* out, const mel_image_format* f, i32 w, i32 h, const Mel_Alloc* a);

void mel_image__packed_to_canonical(const mel_image_format* f, const Mel_Image* src, i32 y, mel_image_canon out);
void mel_image__packed_from_canonical(const mel_image_format* f, Mel_Image* dst, i32 y, mel_image_canon in);
void mel_image__yuv_to_canonical(const mel_image_format* f, const Mel_Image* src, i32 y, mel_image_canon out);
void mel_image__yuv_from_canonical(const mel_image_format* f, Mel_Image* dst, i32 y, mel_image_canon in);
void mel_image__packed_yuv_to_canonical(const mel_image_format* f, const Mel_Image* src, i32 y, mel_image_canon out);
void mel_image__packed_yuv_from_canonical(const mel_image_format* f, Mel_Image* dst, i32 y, mel_image_canon in);

typedef void (*mel_image_kernel)(const Mel_Image* src, Mel_Image* dst);
mel_image_kernel mel_image__find_kernel(const mel_image_format* s, const mel_image_format* d);
