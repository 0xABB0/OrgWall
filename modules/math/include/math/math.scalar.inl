#ifdef _CLANGD
#pragma once
#include "math.scalar.h"
#endif

static inline f32 mel_minf(f32 a, f32 b)
{
    return a < b ? a : b;
}

static inline f32 mel_maxf(f32 a, f32 b)
{
    return a > b ? a : b;
}

static inline i32 mel_mini(i32 a, i32 b)
{
    return a < b ? a : b;
}

static inline i32 mel_maxi(i32 a, i32 b)
{
    return a > b ? a : b;
}

static inline f32 mel_clampf(f32 x, f32 lo, f32 hi)
{
    return mel_minf(mel_maxf(x, lo), hi);
}

static inline i32 mel_clampi(i32 x, i32 lo, i32 hi)
{
    return mel_mini(mel_maxi(x, lo), hi);
}

static inline f32 mel_absf(f32 x)
{
    return __builtin_fabsf(x);
}

static inline f32 mel_signf(f32 x)
{
    return (x > 0.0f) ? 1.0f : (x < 0.0f) ? -1.0f : 0.0f;
}

static inline f32 mel_sqrtf(f32 x)
{
    return __builtin_sqrtf(x);
}

static inline f32 mel_rsqrtf(f32 x)
{
    return 1.0f / __builtin_sqrtf(x);
}

static inline f32 mel_sinf(f32 x)
{
    return __builtin_sinf(x);
}

static inline f32 mel_cosf(f32 x)
{
    return __builtin_cosf(x);
}

static inline f32 mel_tanf(f32 x)
{
    return __builtin_tanf(x);
}

static inline f32 mel_asinf(f32 x)
{
    return __builtin_asinf(x);
}

static inline f32 mel_acosf(f32 x)
{
    return __builtin_acosf(x);
}

static inline f32 mel_atanf(f32 x)
{
    return __builtin_atanf(x);
}

static inline f32 mel_atan2f(f32 y, f32 x)
{
    return __builtin_atan2f(y, x);
}

static inline f32 mel_sinhf(f32 x)
{
    return __builtin_sinhf(x);
}

static inline f32 mel_coshf(f32 x)
{
    return __builtin_coshf(x);
}

static inline f32 mel_tanhf(f32 x)
{
    return __builtin_tanhf(x);
}

static inline f32 mel_expf(f32 x)
{
    return __builtin_expf(x);
}

static inline f32 mel_logf(f32 x)
{
    return __builtin_logf(x);
}

static inline f32 mel_exp2f(f32 x)
{
    return __builtin_exp2f(x);
}

static inline f32 mel_log2f(f32 x)
{
    return __builtin_log2f(x);
}

static inline f32 mel_powf(f32 x, f32 y)
{
    return __builtin_powf(x, y);
}

static inline f32 mel_floorf(f32 x)
{
    return __builtin_floorf(x);
}

static inline f32 mel_ceilf(f32 x)
{
    return __builtin_ceilf(x);
}

static inline f32 mel_roundf(f32 x)
{
    return __builtin_roundf(x);
}

static inline f32 mel_truncf(f32 x)
{
    return __builtin_truncf(x);
}

static inline f32 mel_copysignf(f32 x, f32 y)
{
    return __builtin_copysignf(x, y);
}

static inline f32 mel_toradf(f32 deg)
{
    return deg * MEL_DEG2RAD;
}

static inline f32 mel_todegf(f32 rad)
{
    return rad * MEL_RAD2DEG;
}

static inline f32 mel_lerpf(f32 a, f32 b, f32 t)
{
    return (1.0f - t) * a + t * b;
}

static inline f32 mel_inverselerp(f32 a, f32 b, f32 v)
{
    return (v - a) / (b - a);
}

static inline f32 mel_remapf(f32 in_min, f32 in_max, f32 out_min, f32 out_max, f32 v)
{
    f32 t = mel_inverselerp(in_min, in_max, v);
    return mel_lerpf(out_min, out_max, t);
}

static inline f32 mel_smoothstepf(f32 edge0, f32 edge1, f32 x)
{
    f32 t = mel_saturatef((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

static inline f32 mel_linearstepf(f32 t, f32 min, f32 max)
{
    return mel_saturatef((t - min) / (max - min));
}

static inline f32 mel_biasf(f32 t, f32 bias)
{
    return t / ((1.0f / bias - 2.0f) * (1.0f - t) + 1.0f);
}

static inline f32 mel_gainf(f32 t, f32 gain)
{
    return t < 0.5f
        ? mel_biasf(2.0f * t, gain) * 0.5f
        : mel_biasf(2.0f * t - 1.0f, 1.0f - gain) * 0.5f + 0.5f;
}

static inline f32 mel_stepf(f32 a, f32 edge)
{
    return a < edge ? 0.0f : 1.0f;
}

static inline f32 mel_pulsf(f32 a, f32 start, f32 end)
{
    return mel_stepf(a, start) - mel_stepf(a, end);
}

static inline f32 mel_saturatef(f32 n)
{
    return mel_clampf(n, 0.0f, 1.0f);
}

static inline f32 mel_wrapf(f32 x, f32 wrap)
{
    return x - wrap * __builtin_floorf(x / wrap);
}

static inline f32 mel_wrap_rangef(f32 x, f32 lo, f32 hi)
{
    return lo + mel_wrapf(x - lo, hi - lo);
}

static inline i32 mel_iwrap_range(i32 x, i32 lo, i32 hi)
{
    i32 range = hi - lo;
    i32 m = (x - lo) % range;
    return m < 0 ? m + range + lo : m + lo;
}

static inline f32 mel_fractf(f32 x)
{
    return x - __builtin_floorf(x);
}

static inline f32 mel_modf(f32 a, f32 b)
{
    return a - b * __builtin_floorf(a / b);
}

static inline f32 mel_normalize_time(f32 t, f32 max)
{
    return t / max;
}

static inline bool mel_equalf(f32 a, f32 b, f32 epsilon)
{
    return mel_absf(a - b) <= epsilon;
}

static inline bool mel_isnanf(f32 x)
{
    return __builtin_isnan(x);
}

static inline bool mel_isinff(f32 x)
{
    return __builtin_isinf(x);
}

static inline bool mel_isfinf(f32 x)
{
    return __builtin_isfinite(x);
}

static inline f32 mel_angle_diff(f32 a, f32 b)
{
    f32 d = mel_modf(b - a + MEL_PI, MEL_TAU) - MEL_PI;
    return d;
}

static inline f32 mel_angle_lerp(f32 a, f32 b, f32 t)
{
    return a + mel_angle_diff(a, b) * t;
}

static inline bool mel_is_power_of_two(u32 n)
{
    return n && !(n & (n - 1));
}

static inline u32 mel_next_power_of_two(u32 n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

static inline u32 mel_ftob(f32 v)
{
    union { f32 f; u32 u; } c = { .f = v };
    return c.u;
}

static inline f32 mel_btof(u32 v)
{
    union { u32 u; f32 f; } c = { .u = v };
    return c.f;
}

static inline u64 mel_dtob(f64 v)
{
    union { f64 f; u64 u; } c = { .f = v };
    return c.u;
}

static inline f64 mel_btod(u64 v)
{
    union { u64 u; f64 f; } c = { .u = v };
    return c.f;
}
