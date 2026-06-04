#include "format_internal.h"

#include <color/rgba.h>

static mel_image_plane_geom packed_geom(const mel_image_format* f, i32 w, i32 h, i32 plane, u32 align)
{
    (void)plane;
    usize                stride = mel_image__align_up((usize)w * (usize)f->bytes_per_pixel, align);
    mel_image_plane_geom g = { 0, (i32)stride, w, h, f->bytes_per_pixel };
    return g;
}

static mel_image_plane_geom yuv_geom(i32 w, i32 h, i32 plane, u32 align, i32 sx, i32 sy, bool semi)
{
    i32   cw = sx ? (w + 1) / 2 : w;
    i32   ch = sy ? (h + 1) / 2 : h;
    usize ys = mel_image__align_up((usize)w, align);

    if (plane == 0)
    {
        mel_image_plane_geom g = { 0, (i32)ys, w, h, 1 };
        return g;
    }

    usize off_u = ys * (usize)h;

    if (semi)
    {
        usize                uvs = mel_image__align_up((usize)cw * 2, align);
        mel_image_plane_geom g = { off_u, (i32)uvs, cw, ch, 2 };
        return g;
    }

    usize cs = mel_image__align_up((usize)cw, align);

    if (plane == 1)
    {
        mel_image_plane_geom g = { off_u, (i32)cs, cw, ch, 1 };
        return g;
    }

    usize                off_v = off_u + cs * (usize)ch;
    mel_image_plane_geom g = { off_v, (i32)cs, cw, ch, 1 };
    return g;
}

static mel_image_plane_geom geom_nv12(const mel_image_format* f, i32 w, i32 h, i32 plane, u32 align)
{
    (void)f;
    return yuv_geom(w, h, plane, align, 1, 1, true);
}

static mel_image_plane_geom geom_i420(const mel_image_format* f, i32 w, i32 h, i32 plane, u32 align)
{
    (void)f;
    return yuv_geom(w, h, plane, align, 1, 1, false);
}

static mel_image_plane_geom geom_i422(const mel_image_format* f, i32 w, i32 h, i32 plane, u32 align)
{
    (void)f;
    return yuv_geom(w, h, plane, align, 1, 0, false);
}

static mel_image_plane_geom geom_i444(const mel_image_format* f, i32 w, i32 h, i32 plane, u32 align)
{
    (void)f;
    return yuv_geom(w, h, plane, align, 0, 0, false);
}

#define MEL_IMAGE_BT601 .kr = 0.299f, .kg = 0.587f, .kb = 0.114f

const mel_image_format mel_image_rgba8 = {
    .name = "rgba8",
    .plane_count = 1,
    .channels = 4,
    .bytes_per_pixel = 4,
    .off_r = 0,
    .off_g = 1,
    .off_b = 2,
    .off_a = 3,
    .geom = packed_geom,
    .bytes_per_sample = 1,
    .sample_load = mel_image__load_unorm8,
    .sample_store = mel_image__store_unorm8,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_rgba8_srgb = {
    .name = "rgba8_srgb",
    .plane_count = 1,
    .channels = 4,
    .bytes_per_pixel = 4,
    .off_r = 0,
    .off_g = 1,
    .off_b = 2,
    .off_a = 3,
    .geom = packed_geom,
    .bytes_per_sample = 1,
    .sample_load = mel_image__load_unorm8,
    .sample_store = mel_image__store_unorm8,
    .to_linear = mel_color_srgb_to_linear,
    .to_encoded = mel_color_linear_to_srgb,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_rgba8_premul = {
    .name = "rgba8_premul",
    .plane_count = 1,
    .channels = 4,
    .bytes_per_pixel = 4,
    .off_r = 0,
    .off_g = 1,
    .off_b = 2,
    .off_a = 3,
    .premultiplied = true,
    .geom = packed_geom,
    .bytes_per_sample = 1,
    .sample_load = mel_image__load_unorm8,
    .sample_store = mel_image__store_unorm8,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_bgra8 = {
    .name = "bgra8",
    .plane_count = 1,
    .channels = 4,
    .bytes_per_pixel = 4,
    .off_r = 2,
    .off_g = 1,
    .off_b = 0,
    .off_a = 3,
    .geom = packed_geom,
    .bytes_per_sample = 1,
    .sample_load = mel_image__load_unorm8,
    .sample_store = mel_image__store_unorm8,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_rgb8 = {
    .name = "rgb8",
    .plane_count = 1,
    .channels = 3,
    .bytes_per_pixel = 3,
    .off_r = 0,
    .off_g = 1,
    .off_b = 2,
    .off_a = -1,
    .geom = packed_geom,
    .bytes_per_sample = 1,
    .sample_load = mel_image__load_unorm8,
    .sample_store = mel_image__store_unorm8,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_gray8 = {
    .name = "gray8",
    .plane_count = 1,
    .channels = 1,
    .bytes_per_pixel = 1,
    .off_r = 0,
    .off_g = 0,
    .off_b = 0,
    .off_a = -1,
    .has_luma = true,
    .geom = packed_geom,
    .bytes_per_sample = 1,
    .sample_load = mel_image__load_unorm8,
    .sample_store = mel_image__store_unorm8,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_gray16 = {
    .name = "gray16",
    .plane_count = 1,
    .channels = 1,
    .bytes_per_pixel = 2,
    .off_r = 0,
    .off_g = 0,
    .off_b = 0,
    .off_a = -1,
    .geom = packed_geom,
    .bytes_per_sample = 2,
    .sample_load = mel_image__load_unorm16,
    .sample_store = mel_image__store_unorm16,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_r8 = {
    .name = "r8",
    .plane_count = 1,
    .channels = 1,
    .bytes_per_pixel = 1,
    .off_r = 0,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .geom = packed_geom,
    .bytes_per_sample = 1,
    .sample_load = mel_image__load_unorm8,
    .sample_store = mel_image__store_unorm8,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_rg8 = {
    .name = "rg8",
    .plane_count = 1,
    .channels = 2,
    .bytes_per_pixel = 2,
    .off_r = 0,
    .off_g = 1,
    .off_b = -1,
    .off_a = -1,
    .geom = packed_geom,
    .bytes_per_sample = 1,
    .sample_load = mel_image__load_unorm8,
    .sample_store = mel_image__store_unorm8,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_rgba16f = {
    .name = "rgba16f",
    .plane_count = 1,
    .channels = 4,
    .bytes_per_pixel = 8,
    .off_r = 0,
    .off_g = 1,
    .off_b = 2,
    .off_a = 3,
    .geom = packed_geom,
    .bytes_per_sample = 2,
    .sample_load = mel_image__load_f16,
    .sample_store = mel_image__store_f16,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_rgba32f = {
    .name = "rgba32f",
    .plane_count = 1,
    .channels = 4,
    .bytes_per_pixel = 16,
    .off_r = 0,
    .off_g = 1,
    .off_b = 2,
    .off_a = 3,
    .geom = packed_geom,
    .bytes_per_sample = 4,
    .sample_load = mel_image__load_f32,
    .sample_store = mel_image__store_f32,
    .to_linear = mel_image__tf_linear,
    .to_encoded = mel_image__tf_linear,
    .to_canonical = mel_image__packed_to_canonical,
    .from_canonical = mel_image__packed_from_canonical,
};

const mel_image_format mel_image_nv12 = {
    .name = "nv12",
    .plane_count = 2,
    .channels = 3,
    .off_r = -1,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .has_luma = true,
    .planar = true,
    .geom = geom_nv12,
    .yuv = { MEL_IMAGE_BT601, .full_range = false, .u_byte = 0, .v_byte = 1 },
    .to_canonical = mel_image__yuv_to_canonical,
    .from_canonical = mel_image__yuv_from_canonical,
};

const mel_image_format mel_image_nv12_full = {
    .name = "nv12_full",
    .plane_count = 2,
    .channels = 3,
    .off_r = -1,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .has_luma = true,
    .planar = true,
    .geom = geom_nv12,
    .yuv = { MEL_IMAGE_BT601, .full_range = true, .u_byte = 0, .v_byte = 1 },
    .to_canonical = mel_image__yuv_to_canonical,
    .from_canonical = mel_image__yuv_from_canonical,
};

const mel_image_format mel_image_nv21 = {
    .name = "nv21",
    .plane_count = 2,
    .channels = 3,
    .off_r = -1,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .has_luma = true,
    .planar = true,
    .geom = geom_nv12,
    .yuv = { MEL_IMAGE_BT601, .full_range = false, .u_byte = 1, .v_byte = 0 },
    .to_canonical = mel_image__yuv_to_canonical,
    .from_canonical = mel_image__yuv_from_canonical,
};

const mel_image_format mel_image_i420 = {
    .name = "i420",
    .plane_count = 3,
    .channels = 3,
    .off_r = -1,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .has_luma = true,
    .planar = true,
    .geom = geom_i420,
    .yuv = { MEL_IMAGE_BT601, .full_range = false },
    .to_canonical = mel_image__yuv_to_canonical,
    .from_canonical = mel_image__yuv_from_canonical,
};

const mel_image_format mel_image_i422 = {
    .name = "i422",
    .plane_count = 3,
    .channels = 3,
    .off_r = -1,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .has_luma = true,
    .planar = true,
    .geom = geom_i422,
    .yuv = { MEL_IMAGE_BT601, .full_range = false },
    .to_canonical = mel_image__yuv_to_canonical,
    .from_canonical = mel_image__yuv_from_canonical,
};

const mel_image_format mel_image_i444 = {
    .name = "i444",
    .plane_count = 3,
    .channels = 3,
    .off_r = -1,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .has_luma = true,
    .planar = true,
    .geom = geom_i444,
    .yuv = { MEL_IMAGE_BT601, .full_range = false },
    .to_canonical = mel_image__yuv_to_canonical,
    .from_canonical = mel_image__yuv_from_canonical,
};

const mel_image_format mel_image_yuyv = {
    .name = "yuyv",
    .plane_count = 1,
    .channels = 3,
    .bytes_per_pixel = 2,
    .off_r = -1,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .has_luma = false,
    .geom = packed_geom,
    .yuv = { MEL_IMAGE_BT601, .full_range = false, .packed = true, .y0_byte = 0, .y1_byte = 2, .pu_byte = 1, .pv_byte = 3 },
    .to_canonical = mel_image__packed_yuv_to_canonical,
    .from_canonical = mel_image__packed_yuv_from_canonical,
};

const mel_image_format mel_image_uyvy = {
    .name = "uyvy",
    .plane_count = 1,
    .channels = 3,
    .bytes_per_pixel = 2,
    .off_r = -1,
    .off_g = -1,
    .off_b = -1,
    .off_a = -1,
    .has_luma = false,
    .geom = packed_geom,
    .yuv = { MEL_IMAGE_BT601, .full_range = false, .packed = true, .y0_byte = 1, .y1_byte = 3, .pu_byte = 0, .pv_byte = 2 },
    .to_canonical = mel_image__packed_yuv_to_canonical,
    .from_canonical = mel_image__packed_yuv_from_canonical,
};

i32 mel_image_format_plane_count(const mel_image_format* f) { return f ? f->plane_count : 0; }

i32 mel_image_format_channels(const mel_image_format* f) { return f ? f->channels : 0; }

const char* mel_image_format_name(const mel_image_format* f) { return f ? f->name : ""; }

bool mel_image_format_has_luma(const mel_image_format* f) { return f ? f->has_luma : false; }
