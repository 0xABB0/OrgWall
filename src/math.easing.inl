#ifdef _CLANGD
#pragma once
#include "math.easing.h"
#endif

static inline f32 mel_ease_linear(f32 t) { return t; }

static inline f32 mel_ease_in_quad(f32 t) { return t * t; }
static inline f32 mel_ease_out_quad(f32 t) { return 1.0f - (1.0f - t) * (1.0f - t); }
static inline f32 mel_ease_in_out_quad(f32 t) { return t < 0.5f ? 2.0f * t * t : 1.0f - mel_powf(-2.0f * t + 2.0f, 2.0f) * 0.5f; }

static inline f32 mel_ease_in_cubic(f32 t) { return t * t * t; }
static inline f32 mel_ease_out_cubic(f32 t) { f32 u = 1.0f - t; return 1.0f - u * u * u; }
static inline f32 mel_ease_in_out_cubic(f32 t) { return t < 0.5f ? 4.0f * t * t * t : 1.0f - mel_powf(-2.0f * t + 2.0f, 3.0f) * 0.5f; }

static inline f32 mel_ease_in_quart(f32 t) { return t * t * t * t; }
static inline f32 mel_ease_out_quart(f32 t) { f32 u = 1.0f - t; return 1.0f - u * u * u * u; }
static inline f32 mel_ease_in_out_quart(f32 t) { return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - mel_powf(-2.0f * t + 2.0f, 4.0f) * 0.5f; }

static inline f32 mel_ease_in_quint(f32 t) { return t * t * t * t * t; }
static inline f32 mel_ease_out_quint(f32 t) { f32 u = 1.0f - t; return 1.0f - u * u * u * u * u; }
static inline f32 mel_ease_in_out_quint(f32 t) { return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - mel_powf(-2.0f * t + 2.0f, 5.0f) * 0.5f; }

static inline f32 mel_ease_in_sine(f32 t) { return 1.0f - mel_cosf(t * MEL_HALF_PI); }
static inline f32 mel_ease_out_sine(f32 t) { return mel_sinf(t * MEL_HALF_PI); }
static inline f32 mel_ease_in_out_sine(f32 t) { return -(mel_cosf(MEL_PI * t) - 1.0f) * 0.5f; }

static inline f32 mel_ease_in_circ(f32 t) { return 1.0f - mel_sqrtf(1.0f - t * t); }
static inline f32 mel_ease_out_circ(f32 t) { return mel_sqrtf(1.0f - (t - 1.0f) * (t - 1.0f)); }
static inline f32 mel_ease_in_out_circ(f32 t)
{
    return t < 0.5f
        ? (1.0f - mel_sqrtf(1.0f - mel_powf(2.0f * t, 2.0f))) * 0.5f
        : (mel_sqrtf(1.0f - mel_powf(-2.0f * t + 2.0f, 2.0f)) + 1.0f) * 0.5f;
}

static inline f32 mel_ease_in_expo(f32 t) { return t == 0.0f ? 0.0f : mel_powf(2.0f, 10.0f * t - 10.0f); }
static inline f32 mel_ease_out_expo(f32 t) { return t == 1.0f ? 1.0f : 1.0f - mel_powf(2.0f, -10.0f * t); }
static inline f32 mel_ease_in_out_expo(f32 t)
{
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return t < 0.5f
        ? mel_powf(2.0f, 20.0f * t - 10.0f) * 0.5f
        : (2.0f - mel_powf(2.0f, -20.0f * t + 10.0f)) * 0.5f;
}

static inline f32 mel_ease_in_elastic(f32 t)
{
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return -mel_powf(2.0f, 10.0f * t - 10.0f) * mel_sinf((t * 10.0f - 10.75f) * (MEL_TAU / 3.0f));
}

static inline f32 mel_ease_out_elastic(f32 t)
{
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return mel_powf(2.0f, -10.0f * t) * mel_sinf((t * 10.0f - 0.75f) * (MEL_TAU / 3.0f)) + 1.0f;
}

static inline f32 mel_ease_in_out_elastic(f32 t)
{
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return t < 0.5f
        ? -(mel_powf(2.0f, 20.0f * t - 10.0f) * mel_sinf((20.0f * t - 11.125f) * (MEL_TAU / 4.5f))) * 0.5f
        : (mel_powf(2.0f, -20.0f * t + 10.0f) * mel_sinf((20.0f * t - 11.125f) * (MEL_TAU / 4.5f))) * 0.5f + 1.0f;
}

static inline f32 mel_ease_in_back(f32 t)
{
    const f32 c1 = 1.70158f;
    const f32 c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}

static inline f32 mel_ease_out_back(f32 t)
{
    const f32 c1 = 1.70158f;
    const f32 c3 = c1 + 1.0f;
    f32 u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

static inline f32 mel_ease_in_out_back(f32 t)
{
    const f32 c1 = 1.70158f;
    const f32 c2 = c1 * 1.525f;
    return t < 0.5f
        ? (mel_powf(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
        : (mel_powf(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
}

static inline f32 mel_ease_out_bounce(f32 t)
{
    const f32 n1 = 7.5625f;
    const f32 d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

static inline f32 mel_ease_in_bounce(f32 t) { return 1.0f - mel_ease_out_bounce(1.0f - t); }
static inline f32 mel_ease_in_out_bounce(f32 t)
{
    return t < 0.5f
        ? (1.0f - mel_ease_out_bounce(1.0f - 2.0f * t)) * 0.5f
        : (1.0f + mel_ease_out_bounce(2.0f * t - 1.0f)) * 0.5f;
}
