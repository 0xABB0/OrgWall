#pragma once

#include "core.types.h"

#define MEL_PI          3.14159265358979323846f
#define MEL_TAU         6.28318530717958647692f
#define MEL_PI2         6.28318530717958647692f
#define MEL_HALF_PI     1.57079632679489661923f
#define MEL_QUARTER_PI  0.78539816339744830962f
#define MEL_E           2.71828182845904523536f
#define MEL_SQRT2       1.41421356237309504880f
#define MEL_DEG2RAD     (MEL_PI / 180.0f)
#define MEL_RAD2DEG     (180.0f / MEL_PI)
#define MEL_EPSILON     1e-6f
#define MEL_FLOAT_MIN   1.175494351e-38f
#define MEL_FLOAT_MAX   3.402823466e+38f
#define MEL_INVLOG_NAT2 1.4426950408889634f
#define MEL_LOG_NAT2    0.6931471805599453f

[[nodiscard]] static inline f32  mel_minf(f32 a, f32 b);
[[nodiscard]] static inline f32  mel_maxf(f32 a, f32 b);
[[nodiscard]] static inline i32  mel_mini(i32 a, i32 b);
[[nodiscard]] static inline i32  mel_maxi(i32 a, i32 b);
[[nodiscard]] static inline f32  mel_clampf(f32 x, f32 lo, f32 hi);
[[nodiscard]] static inline i32  mel_clampi(i32 x, i32 lo, i32 hi);
[[nodiscard]] static inline f32  mel_absf(f32 x);
[[nodiscard]] static inline f32  mel_signf(f32 x);

[[nodiscard]] static inline f32  mel_sqrtf(f32 x);
[[nodiscard]] static inline f32  mel_rsqrtf(f32 x);
[[nodiscard]] static inline f32  mel_sinf(f32 x);
[[nodiscard]] static inline f32  mel_cosf(f32 x);
[[nodiscard]] static inline f32  mel_tanf(f32 x);
[[nodiscard]] static inline f32  mel_asinf(f32 x);
[[nodiscard]] static inline f32  mel_acosf(f32 x);
[[nodiscard]] static inline f32  mel_atanf(f32 x);
[[nodiscard]] static inline f32  mel_atan2f(f32 y, f32 x);
[[nodiscard]] static inline f32  mel_sinhf(f32 x);
[[nodiscard]] static inline f32  mel_coshf(f32 x);
[[nodiscard]] static inline f32  mel_tanhf(f32 x);
[[nodiscard]] static inline f32  mel_expf(f32 x);
[[nodiscard]] static inline f32  mel_logf(f32 x);
[[nodiscard]] static inline f32  mel_exp2f(f32 x);
[[nodiscard]] static inline f32  mel_log2f(f32 x);
[[nodiscard]] static inline f32  mel_powf(f32 x, f32 y);
[[nodiscard]] static inline f32  mel_floorf(f32 x);
[[nodiscard]] static inline f32  mel_ceilf(f32 x);
[[nodiscard]] static inline f32  mel_roundf(f32 x);
[[nodiscard]] static inline f32  mel_truncf(f32 x);
[[nodiscard]] static inline f32  mel_copysignf(f32 x, f32 y);

[[nodiscard]] static inline f32  mel_toradf(f32 deg);
[[nodiscard]] static inline f32  mel_todegf(f32 rad);

[[nodiscard]] static inline f32  mel_lerpf(f32 a, f32 b, f32 t);
[[nodiscard]] static inline f32  mel_inverselerp(f32 a, f32 b, f32 v);
[[nodiscard]] static inline f32  mel_remapf(f32 in_min, f32 in_max, f32 out_min, f32 out_max, f32 v);
[[nodiscard]] static inline f32  mel_smoothstepf(f32 edge0, f32 edge1, f32 x);
[[nodiscard]] static inline f32  mel_linearstepf(f32 t, f32 min, f32 max);
[[nodiscard]] static inline f32  mel_biasf(f32 t, f32 bias);
[[nodiscard]] static inline f32  mel_gainf(f32 t, f32 gain);

[[nodiscard]] static inline f32  mel_stepf(f32 a, f32 edge);
[[nodiscard]] static inline f32  mel_pulsf(f32 a, f32 start, f32 end);
[[nodiscard]] static inline f32  mel_saturatef(f32 n);
[[nodiscard]] static inline f32  mel_wrapf(f32 x, f32 wrap);
[[nodiscard]] static inline f32  mel_wrap_rangef(f32 x, f32 lo, f32 hi);
[[nodiscard]] static inline i32  mel_iwrap_range(i32 x, i32 lo, i32 hi);

[[nodiscard]] static inline f32  mel_fractf(f32 x);
[[nodiscard]] static inline f32  mel_modf(f32 a, f32 b);
[[nodiscard]] static inline f32  mel_normalize_time(f32 t, f32 max);

[[nodiscard]] static inline bool mel_equalf(f32 a, f32 b, f32 epsilon);
[[nodiscard]] static inline bool mel_isnanf(f32 x);
[[nodiscard]] static inline bool mel_isinff(f32 x);
[[nodiscard]] static inline bool mel_isfinf(f32 x);

[[nodiscard]] static inline f32  mel_angle_diff(f32 a, f32 b);
[[nodiscard]] static inline f32  mel_angle_lerp(f32 a, f32 b, f32 t);

[[nodiscard]] static inline bool mel_is_power_of_two(u32 n);
[[nodiscard]] static inline u32  mel_next_power_of_two(u32 n);
[[nodiscard]] static inline u32  mel_ftob(f32 v);
[[nodiscard]] static inline f32  mel_btof(u32 v);
[[nodiscard]] static inline u64  mel_dtob(f64 v);
[[nodiscard]] static inline f64  mel_btod(u64 v);

#include "math.scalar.inl"
