#include <color/space.h>

#include <color/adapt.h>
#include <color/rgba.h>

#include "color_math.h"

#include <math.h>

static float mel__tf_linear(float c) { return c; }

static float mel__eotf_gamma22(float c) { return c < 0.0f ? -powf(-c, 2.19921875f) : powf(c, 2.19921875f); }

static float mel__oetf_gamma22(float c) { return c < 0.0f ? -powf(-c, 1.0f / 2.19921875f) : powf(c, 1.0f / 2.19921875f); }

static float mel__eotf_prophoto(float c)
{
    float a = c < 0.0f ? -c : c;
    float sign = c < 0.0f ? -1.0f : 1.0f;
    if (a < 16.0f / 512.0f)
        return sign * a / 16.0f;
    return sign * powf(a, 1.8f);
}

static float mel__oetf_prophoto(float c)
{
    float a = c < 0.0f ? -c : c;
    float sign = c < 0.0f ? -1.0f : 1.0f;
    if (a < 1.0f / 512.0f)
        return sign * a * 16.0f;
    return sign * powf(a, 1.0f / 1.8f);
}

static float mel__eotf_rec2020(float e)
{
    const float a = 1.09929682680944f;
    const float b = 0.018053968510807f;
    float       s = e < 0.0f ? -1.0f : 1.0f;
    float       v = e < 0.0f ? -e : e;
    if (v < b * 4.5f)
        return s * v / 4.5f;
    return s * powf((v + (a - 1.0f)) / a, 1.0f / 0.45f);
}

static float mel__oetf_rec2020(float l)
{
    const float a = 1.09929682680944f;
    const float b = 0.018053968510807f;
    float       s = l < 0.0f ? -1.0f : 1.0f;
    float       v = l < 0.0f ? -l : l;
    if (v < b)
        return s * 4.5f * v;
    return s * (a * powf(v, 0.45f) - (a - 1.0f));
}

static float mel__eotf_pq(float e)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    float       v = e < 0.0f ? 0.0f : e;
    float       p = powf(v, 1.0f / m2);
    float       num = p - c1;
    if (num < 0.0f)
        num = 0.0f;
    return powf(num / (c2 - c3 * p), 1.0f / m1);
}

static float mel__oetf_pq(float l)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    float       v = l < 0.0f ? 0.0f : l;
    float       p = powf(v, m1);
    return powf((c1 + c2 * p) / (1.0f + c3 * p), m2);
}

static float mel__eotf_hlg(float e)
{
    const float a = 0.17883277f;
    const float b = 0.28466892f;
    const float c = 0.55991073f;
    float       v = e < 0.0f ? 0.0f : e;
    if (v <= 0.5f)
        return v * v / 3.0f;
    return (expf((v - c) / a) + b) / 12.0f;
}

static float mel__oetf_hlg(float l)
{
    const float a = 0.17883277f;
    const float b = 0.28466892f;
    const float c = 0.55991073f;
    float       v = l < 0.0f ? 0.0f : l;
    if (v <= 1.0f / 12.0f)
        return sqrtf(3.0f * v);
    return a * logf(12.0f * v - b) + c;
}

mel_white_point mel_white_d65(void) { return (mel_white_point){ 0.3127f, 0.3290f }; }

mel_white_point mel_white_d50(void) { return (mel_white_point){ 0.34567f, 0.35850f }; }

mel_white_point mel_white_aces(void) { return (mel_white_point){ 0.32168f, 0.33767f }; }

mel_white_point mel_white_e(void) { return (mel_white_point){ 1.0f / 3.0f, 1.0f / 3.0f }; }

mel_xyz mel_white_point_xyz(mel_white_point w)
{
    Mel_Vec3 v = mel__chromaticity_xyz(w.x, w.y);
    return (mel_xyz){ v.x, v.y, v.z };
}

mel_color_space mel_color_space_srgb(void)
{
    return (mel_color_space){
        { 0.640f, 0.330f }, { 0.300f, 0.600f }, { 0.150f, 0.060f }, mel_white_d65(), mel_color_srgb_to_linear, mel_color_linear_to_srgb,
    };
}

mel_color_space mel_color_space_linear_srgb(void)
{
    return (mel_color_space){
        { 0.640f, 0.330f }, { 0.300f, 0.600f }, { 0.150f, 0.060f }, mel_white_d65(), mel__tf_linear, mel__tf_linear,
    };
}

mel_color_space mel_color_space_display_p3(void)
{
    return (mel_color_space){
        { 0.680f, 0.320f }, { 0.265f, 0.690f }, { 0.150f, 0.060f }, mel_white_d65(), mel_color_srgb_to_linear, mel_color_linear_to_srgb,
    };
}

mel_color_space mel_color_space_rec2020(void)
{
    return (mel_color_space){
        { 0.708f, 0.292f }, { 0.170f, 0.797f }, { 0.131f, 0.046f }, mel_white_d65(), mel__eotf_rec2020, mel__oetf_rec2020,
    };
}

mel_color_space mel_color_space_rec2020_pq(void)
{
    return (mel_color_space){
        { 0.708f, 0.292f }, { 0.170f, 0.797f }, { 0.131f, 0.046f }, mel_white_d65(), mel__eotf_pq, mel__oetf_pq,
    };
}

mel_color_space mel_color_space_rec2020_hlg(void)
{
    return (mel_color_space){
        { 0.708f, 0.292f }, { 0.170f, 0.797f }, { 0.131f, 0.046f }, mel_white_d65(), mel__eotf_hlg, mel__oetf_hlg,
    };
}

mel_color_space mel_color_space_adobe_rgb(void)
{
    return (mel_color_space){
        { 0.640f, 0.330f }, { 0.210f, 0.710f }, { 0.150f, 0.060f }, mel_white_d65(), mel__eotf_gamma22, mel__oetf_gamma22,
    };
}

mel_color_space mel_color_space_prophoto(void)
{
    return (mel_color_space){
        { 0.7347f, 0.2653f }, { 0.1596f, 0.8404f }, { 0.0366f, 0.0001f }, mel_white_d50(), mel__eotf_prophoto, mel__oetf_prophoto,
    };
}

mel_color_space mel_color_space_aces_cg(void)
{
    return (mel_color_space){
        { 0.713f, 0.293f }, { 0.165f, 0.830f }, { 0.128f, 0.044f }, mel_white_aces(), mel__tf_linear, mel__tf_linear,
    };
}

mel_color_space mel_color_space_aces2065_1(void)
{
    return (mel_color_space){
        { 0.7347f, 0.2653f }, { 0.0f, 1.0f }, { 0.0001f, -0.0770f }, mel_white_aces(), mel__tf_linear, mel__tf_linear,
    };
}

static void mel__space_ensure_matrices(const mel_color_space* s)
{
    if (s->matrices_valid)
        return;
    mel_color_space* m = (mel_color_space*)s;
    Mel_Vec3         r = mel__chromaticity_xyz(s->red.x, s->red.y);
    Mel_Vec3         g = mel__chromaticity_xyz(s->green.x, s->green.y);
    Mel_Vec3         b = mel__chromaticity_xyz(s->blue.x, s->blue.y);
    Mel_Mat3         primaries = mel__mat3_cols(r, g, b);
    Mel_Vec3         w = mel__chromaticity_xyz(s->white.x, s->white.y);
    Mel_Vec3         scale = mel_mat3_mul_vec3(mel_mat3_inverse(primaries), w);
    Mel_Mat3         to = mel__mat3_cols(mel__vec3_scale(r, scale.x), mel__vec3_scale(g, scale.y), mel__vec3_scale(b, scale.z));
    Mel_Mat3         from = mel_mat3_inverse(to);
    for (int i = 0; i < 9; i++)
    {
        m->to_xyz[i] = to.e[i];
        m->from_xyz[i] = from.e[i];
    }
    m->matrices_valid = 1;
}

static Mel_Mat3 mel__space_to_xyz(const mel_color_space* s)
{
    mel__space_ensure_matrices(s);
    Mel_Mat3 m;
    for (int i = 0; i < 9; i++)
        m.e[i] = s->to_xyz[i];
    return m;
}

static Mel_Mat3 mel__space_from_xyz(const mel_color_space* s)
{
    mel__space_ensure_matrices(s);
    Mel_Mat3 m;
    for (int i = 0; i < 9; i++)
        m.e[i] = s->from_xyz[i];
    return m;
}

mel_xyz mel_linear_rgb_to_xyz(mel_color linear, const mel_color_space* s)
{
    Mel_Vec3 v = mel_mat3_mul_vec3(mel__space_to_xyz(s), mel__vec3(linear.r, linear.g, linear.b));
    return (mel_xyz){ v.x, v.y, v.z };
}

mel_color mel_xyz_to_linear_rgb(mel_xyz c, const mel_color_space* s, float a)
{
    Mel_Vec3 v = mel_mat3_mul_vec3(mel__space_from_xyz(s), mel__vec3(c.x, c.y, c.z));
    return (mel_color){ v.x, v.y, v.z, a };
}

mel_color mel_color_convert(mel_color c, const mel_color_space* from, const mel_color_space* to)
{
    mel_xyz xyz = mel_linear_rgb_to_xyz(c, from);
    mel_xyz adapted = mel_xyz_adapt_bradford(xyz, from->white, to->white);
    return mel_xyz_to_linear_rgb(adapted, to, c.a);
}

static uint8_t mel__quant8(float v)
{
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return (uint8_t)(v * 255.0f + 0.5f);
}

mel_color8 mel_color_to_8_in(mel_color linear, const mel_color_space* s)
{
    return (mel_color8){
        mel__quant8(s->to_encoded(linear.r)),
        mel__quant8(s->to_encoded(linear.g)),
        mel__quant8(s->to_encoded(linear.b)),
        mel__quant8(linear.a),
    };
}

mel_color mel_color_from_8_in(mel_color8 enc, const mel_color_space* s)
{
    return (mel_color){
        s->to_linear((float)enc.r / 255.0f),
        s->to_linear((float)enc.g / 255.0f),
        s->to_linear((float)enc.b / 255.0f),
        (float)enc.a / 255.0f,
    };
}

mel_p3 mel_color_to_p3(mel_color c)
{
    mel_color_space from = mel_color_space_srgb();
    mel_color_space to = mel_color_space_display_p3();
    mel_color       o = mel_color_convert(c, &from, &to);
    return (mel_p3){ o.r, o.g, o.b, o.a };
}

mel_color mel_color_from_p3(mel_p3 c)
{
    mel_color_space from = mel_color_space_display_p3();
    mel_color_space to = mel_color_space_srgb();
    return mel_color_convert((mel_color){ c.r, c.g, c.b, c.a }, &from, &to);
}

mel_rec2020 mel_color_to_rec2020(mel_color c)
{
    mel_color_space from = mel_color_space_srgb();
    mel_color_space to = mel_color_space_rec2020();
    mel_color       o = mel_color_convert(c, &from, &to);
    return (mel_rec2020){ o.r, o.g, o.b, o.a };
}

mel_color mel_color_from_rec2020(mel_rec2020 c)
{
    mel_color_space from = mel_color_space_rec2020();
    mel_color_space to = mel_color_space_srgb();
    return mel_color_convert((mel_color){ c.r, c.g, c.b, c.a }, &from, &to);
}

mel_aces_cg mel_color_to_aces_cg(mel_color c)
{
    mel_color_space from = mel_color_space_srgb();
    mel_color_space to = mel_color_space_aces_cg();
    mel_color       o = mel_color_convert(c, &from, &to);
    return (mel_aces_cg){ o.r, o.g, o.b, o.a };
}

mel_color mel_color_from_aces_cg(mel_aces_cg c)
{
    mel_color_space from = mel_color_space_aces_cg();
    mel_color_space to = mel_color_space_srgb();
    return mel_color_convert((mel_color){ c.r, c.g, c.b, c.a }, &from, &to);
}
