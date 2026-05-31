#include <color/rgba.h>

#include <color/hsl.h>
#include <color/oklab.h>

#include "color_internal.h"

#include <math.h>

mel_color mel_color_rgb(float r, float g, float b) {
    return (mel_color){r, g, b, 1.0f};
}

mel_color mel_color_rgba(float r, float g, float b, float a) {
    return (mel_color){r, g, b, a};
}

mel_color mel_color_gray(float v) {
    return (mel_color){v, v, v, 1.0f};
}

float mel_color_srgb_to_linear(float c) {
    if (c <= 0.04045f)
        return c / 12.92f;
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

float mel_color_linear_to_srgb(float c) {
    if (c <= 0.0031308f)
        return 12.92f * c;
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

mel_color mel_color_with_alpha(mel_color c, float a) {
    return (mel_color){c.r, c.g, c.b, a};
}

mel_color mel_color_scale(mel_color c, float s) {
    return (mel_color){c.r * s, c.g * s, c.b * s, c.a};
}

mel_color mel_color_clamp01(mel_color c) {
    return (mel_color){mel__sat(c.r), mel__sat(c.g), mel__sat(c.b), mel__sat(c.a)};
}

mel_color mel_color_lerp(mel_color a, mel_color b, float t) {
    return (mel_color){
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t,
    };
}

mel_color mel_color_mix(mel_color a, mel_color b, float t) {
    mel_oklab oa = mel_color_to_oklab(a);
    mel_oklab ob = mel_color_to_oklab(b);
    mel_oklab mixed = {
        oa.l + (ob.l - oa.l) * t,
        oa.a + (ob.a - oa.a) * t,
        oa.b + (ob.b - oa.b) * t,
    };
    return mel_color_from_oklab(mixed, a.a + (b.a - a.a) * t);
}

mel_color mel_color_over(mel_color src, mel_color dst) {
    float a = src.a + dst.a * (1.0f - src.a);
    if (a <= 0.0f)
        return (mel_color){0.0f, 0.0f, 0.0f, 0.0f};
    float inv = 1.0f - src.a;
    return (mel_color){
        (src.r * src.a + dst.r * dst.a * inv) / a,
        (src.g * src.a + dst.g * dst.a * inv) / a,
        (src.b * src.a + dst.b * dst.a * inv) / a,
        a,
    };
}

float mel_color_luminance(mel_color c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

float mel_color_contrast(mel_color a, mel_color b) {
    float la = mel_color_luminance(a);
    float lb = mel_color_luminance(b);
    float hi = fmaxf(la, lb);
    float lo = fminf(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

mel_color mel_color_grayscale(mel_color c) {
    float y = mel_color_luminance(c);
    return (mel_color){y, y, y, c.a};
}

mel_color mel_color_lighten(mel_color c, float amount) {
    mel_hsl hsl = mel_color_to_hsl(c);
    hsl.l = mel__sat(hsl.l + amount);
    return mel_color_from_hsl(hsl, c.a);
}

mel_color mel_color_darken(mel_color c, float amount) {
    return mel_color_lighten(c, -amount);
}

mel_color mel_color_saturate(mel_color c, float amount) {
    mel_hsl hsl = mel_color_to_hsl(c);
    hsl.s = mel__sat(hsl.s + amount);
    return mel_color_from_hsl(hsl, c.a);
}

mel_color mel_color_desaturate(mel_color c, float amount) {
    return mel_color_saturate(c, -amount);
}
