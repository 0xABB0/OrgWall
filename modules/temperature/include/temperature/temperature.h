#pragma once

#include <stdint.h>

#include <gmp.h>
#include <mpfr.h>

#include <core/compiler.h>
#include <math/real.h>

typedef struct Mel_Degrees Mel_Degrees;

struct Mel_Degrees
{
    Mel_Real v;
};

static inline void mel_degrees_view(mpfr_t out, const Mel_Degrees* t);

MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_celsius(double value);
MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_celsius(mpfr_srcptr value);
MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_fahrenheit(double value);
MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_fahrenheit(mpfr_srcptr value);
MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_kelvin(double value);
MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_kelvin(mpfr_srcptr value);

MEL_NODISCARD static inline double mel_degrees_to_celsius(Mel_Degrees t);
MEL_NODISCARD static inline double mel_degrees_to_fahrenheit(Mel_Degrees t);
MEL_NODISCARD static inline double mel_degrees_to_kelvin(Mel_Degrees t);

MEL_NODISCARD static inline Mel_Degrees mel_degrees_add(Mel_Degrees a, Mel_Degrees b);
MEL_NODISCARD static inline Mel_Degrees mel_degrees_sub(Mel_Degrees a, Mel_Degrees b);
MEL_NODISCARD static inline Mel_Degrees mel_degrees_neg(Mel_Degrees t);

MEL_NODISCARD MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_mul(Mel_Degrees a, mpfr_srcptr b);
MEL_NODISCARD MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_mul(Mel_Degrees a, double b);

MEL_NODISCARD MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_div(Mel_Degrees a, mpfr_srcptr b);
MEL_NODISCARD MEL_OVERLOADABLE static inline Mel_Degrees mel_degrees_div(Mel_Degrees a, double b);

MEL_NODISCARD MEL_OVERLOADABLE static inline double mel_degrees_ratio(Mel_Degrees a, Mel_Degrees b);
MEL_OVERLOADABLE static inline void                 mel_degrees_ratio(mpfr_ptr out, Mel_Degrees a, Mel_Degrees b);

MEL_NODISCARD static inline Mel_Degrees mel_degrees_abs(Mel_Degrees t);
MEL_NODISCARD static inline Mel_Degrees mel_degrees_min(Mel_Degrees a, Mel_Degrees b);
MEL_NODISCARD static inline Mel_Degrees mel_degrees_max(Mel_Degrees a, Mel_Degrees b);
MEL_NODISCARD static inline Mel_Degrees mel_degrees_midpoint(Mel_Degrees a, Mel_Degrees b);

MEL_NODISCARD static inline uint8_t mel_degrees_cmp(Mel_Degrees a, Mel_Degrees b);
MEL_NODISCARD static inline uint8_t mel_degrees_eq(Mel_Degrees a, Mel_Degrees b);
MEL_NODISCARD static inline uint8_t mel_degrees_near(Mel_Degrees a, Mel_Degrees b, mpfr_srcptr tolerance);
MEL_NODISCARD static inline uint8_t mel_degrees_is_absolute_zero(Mel_Degrees t);

#include "temperature.inl"
