#ifdef _CLANGD
#pragma once
#include "temperature.h"
#endif

static inline void mel_degrees_view(mpfr_t out, const Mel_Degrees* t) { mel_real_view(out, &t->v); }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_kelvin(double value) { return (Mel_Degrees){ .v = mel_real(value) }; }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_kelvin(mpfr_srcptr value) { return (Mel_Degrees){ .v = mel_real(value) }; }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_celsius(double value) { return (Mel_Degrees){ .v = mel_real_add(mel_real(value), mel_real(27315u, 100u)) }; }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_celsius(mpfr_srcptr value) { return (Mel_Degrees){ .v = mel_real_add(mel_real(value), mel_real(27315u, 100u)) }; }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_fahrenheit(double value)
{
    Mel_Real k = mel_real_add(mel_real(value), mel_real(45967u, 100u));
    k = mel_real_mul_ui(k, 5u);
    k = mel_real_div_ui(k, 9u);
    return (Mel_Degrees){ .v = k };
}

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_fahrenheit(mpfr_srcptr value)
{
    Mel_Real k = mel_real_add(mel_real(value), mel_real(45967u, 100u));
    k = mel_real_mul_ui(k, 5u);
    k = mel_real_div_ui(k, 9u);
    return (Mel_Degrees){ .v = k };
}

static inline double mel_degrees_to_kelvin(Mel_Degrees t) { return mel_real_to_double(t.v); }

static inline double mel_degrees_to_celsius(Mel_Degrees t) { return mel_real_to_double(mel_real_sub(t.v, mel_real(27315u, 100u))); }

static inline double mel_degrees_to_fahrenheit(Mel_Degrees t)
{
    Mel_Real f = mel_real_mul_ui(t.v, 9u);
    f = mel_real_div_ui(f, 5u);
    f = mel_real_sub(f, mel_real(45967u, 100u));
    return mel_real_to_double(f);
}

static inline Mel_Degrees mel_degrees_add(Mel_Degrees a, Mel_Degrees b) { return (Mel_Degrees){ .v = mel_real_add(a.v, b.v) }; }

static inline Mel_Degrees mel_degrees_sub(Mel_Degrees a, Mel_Degrees b) { return (Mel_Degrees){ .v = mel_real_sub(a.v, b.v) }; }

static inline Mel_Degrees mel_degrees_neg(Mel_Degrees t) { return (Mel_Degrees){ .v = mel_real_neg(t.v) }; }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_mul(Mel_Degrees a, mpfr_srcptr b) { return (Mel_Degrees){ .v = mel_real_mul(a.v, b) }; }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_mul(Mel_Degrees a, double b) { return (Mel_Degrees){ .v = mel_real_mul(a.v, b) }; }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_div(Mel_Degrees a, mpfr_srcptr b) { return (Mel_Degrees){ .v = mel_real_div(a.v, b) }; }

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_div(Mel_Degrees a, double b) { return (Mel_Degrees){ .v = mel_real_div(a.v, b) }; }

MEL_OVERLOADABLE static inline double mel_degrees_ratio(Mel_Degrees a, Mel_Degrees b) { return mel_real_ratio(a.v, b.v); }

MEL_OVERLOADABLE static inline void mel_degrees_ratio(mpfr_ptr out, Mel_Degrees a, Mel_Degrees b) { mel_real_ratio(out, a.v, b.v); }

static inline Mel_Degrees mel_degrees_abs(Mel_Degrees t) { return (Mel_Degrees){ .v = mel_real_abs(t.v) }; }

static inline Mel_Degrees mel_degrees_min(Mel_Degrees a, Mel_Degrees b) { return (Mel_Degrees){ .v = mel_real_min(a.v, b.v) }; }

static inline Mel_Degrees mel_degrees_max(Mel_Degrees a, Mel_Degrees b) { return (Mel_Degrees){ .v = mel_real_max(a.v, b.v) }; }

static inline Mel_Degrees mel_degrees_midpoint(Mel_Degrees a, Mel_Degrees b) { return (Mel_Degrees){ .v = mel_real_midpoint(a.v, b.v) }; }

static inline uint8_t mel_degrees_cmp(Mel_Degrees a, Mel_Degrees b) { return mel_real_cmp(a.v, b.v); }

static inline uint8_t mel_degrees_eq(Mel_Degrees a, Mel_Degrees b) { return mel_real_eq(a.v, b.v); }

static inline uint8_t mel_degrees_near(Mel_Degrees a, Mel_Degrees b, mpfr_srcptr tolerance) { return mel_real_near(a.v, b.v, tolerance); }

static inline uint8_t mel_degrees_is_absolute_zero(Mel_Degrees t) { return mel_real_is_zero(t.v); }
