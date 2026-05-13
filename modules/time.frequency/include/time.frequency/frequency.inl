#ifdef _CLANGD
#pragma once
#include "frequency.h"
#endif

static inline void mel_freq_view(mpfr_t out, const Mel_Hz *f)
{
  mel_real_view(out, &f->v);
}

MEL_OVERLOADABLE static inline Mel_Hz mel_freq(double value)
{
  return (Mel_Hz){ .v = mel_real(value) };
}

MEL_OVERLOADABLE static inline Mel_Hz mel_freq(mpfr_srcptr value)
{
  return (Mel_Hz){ .v = mel_real(value) };
}

MEL_OVERLOADABLE static inline Mel_Hz mel_freq(mpq_srcptr value)
{
  return (Mel_Hz){ .v = mel_real(value) };
}

MEL_OVERLOADABLE static inline Mel_Hz mel_freq(unsigned num, unsigned den)
{
  return (Mel_Hz){ .v = mel_real(num, den) };
}

static inline double mel_freq_to_double(Mel_Hz f)
{
  return mel_real_to_double(f.v);
}

static inline Mel_Hz mel_freq_add(Mel_Hz a, Mel_Hz b)
{
  return (Mel_Hz){ .v = mel_real_add(a.v, b.v) };
}

static inline Mel_Hz mel_freq_sub(Mel_Hz a, Mel_Hz b)
{
  return (Mel_Hz){ .v = mel_real_sub(a.v, b.v) };
}

static inline Mel_Hz mel_freq_neg(Mel_Hz f)
{
  return (Mel_Hz){ .v = mel_real_neg(f.v) };
}

MEL_OVERLOADABLE static inline Mel_Hz mel_freq_mul(Mel_Hz a, mpfr_srcptr b)
{
  return (Mel_Hz){ .v = mel_real_mul(a.v, b) };
}

MEL_OVERLOADABLE static inline Mel_Hz mel_freq_mul(Mel_Hz a, double b)
{
  return (Mel_Hz){ .v = mel_real_mul(a.v, b) };
}

MEL_OVERLOADABLE static inline Mel_Hz mel_freq_div(Mel_Hz a, mpfr_srcptr b)
{
  return (Mel_Hz){ .v = mel_real_div(a.v, b) };
}

MEL_OVERLOADABLE static inline Mel_Hz mel_freq_div(Mel_Hz a, double b)
{
  return (Mel_Hz){ .v = mel_real_div(a.v, b) };
}

MEL_OVERLOADABLE static inline double mel_freq_ratio(Mel_Hz a, Mel_Hz b)
{
  return mel_real_ratio(a.v, b.v);
}

MEL_OVERLOADABLE static inline void mel_freq_ratio(mpfr_ptr out, Mel_Hz a, Mel_Hz b)
{
  mel_real_ratio(out, a.v, b.v);
}

static inline Mel_Hz mel_freq_transpose(Mel_Hz f, mpq_srcptr ratio)
{
  return (Mel_Hz){ .v = mel_real_mul_q(f.v, ratio) };
}

static inline Mel_Hz mel_freq_octave_up(Mel_Hz f)
{
  return (Mel_Hz){ .v = mel_real_mul_ui(f.v, 2) };
}

static inline Mel_Hz mel_freq_octave_down(Mel_Hz f)
{
  return (Mel_Hz){ .v = mel_real_div_ui(f.v, 2) };
}

static inline Mel_Hz mel_freq_harmonic(Mel_Hz f, unsigned n)
{
  return (Mel_Hz){ .v = mel_real_mul_ui(f.v, n) };
}

static inline Mel_Hz mel_freq_midpoint(Mel_Hz a, Mel_Hz b)
{
  return (Mel_Hz){ .v = mel_real_midpoint(a.v, b.v) };
}

static inline Mel_Hz mel_freq_mod(Mel_Hz a, Mel_Hz b)
{
  return (Mel_Hz){ .v = mel_real_mod(a.v, b.v) };
}

static inline Mel_Hz mel_freq_floordiv(Mel_Hz a, Mel_Hz b)
{
  return (Mel_Hz){ .v = mel_real_floordiv(a.v, b.v) };
}

static inline Mel_Hz mel_freq_abs(Mel_Hz f)
{
  return (Mel_Hz){ .v = mel_real_abs(f.v) };
}

static inline Mel_Hz mel_freq_min(Mel_Hz a, Mel_Hz b)
{
  return (Mel_Hz){ .v = mel_real_min(a.v, b.v) };
}

static inline Mel_Hz mel_freq_max(Mel_Hz a, Mel_Hz b)
{
  return (Mel_Hz){ .v = mel_real_max(a.v, b.v) };
}

static inline uint8_t mel_freq_cmp(Mel_Hz a, Mel_Hz b)
{
  return mel_real_cmp(a.v, b.v);
}

static inline uint8_t mel_freq_eq(Mel_Hz a, Mel_Hz b)
{
  return mel_real_eq(a.v, b.v);
}

static inline uint8_t mel_freq_near(Mel_Hz a, Mel_Hz b, mpfr_srcptr tolerance)
{
  return mel_real_near(a.v, b.v, tolerance);
}

static inline uint8_t mel_freq_is_zero(Mel_Hz f)
{
  return mel_real_is_zero(f.v);
}

static inline Mel_Hz mel_freq_beat(Mel_Hz a, Mel_Hz b)
{
  return (Mel_Hz){ .v = mel_real_abs(mel_real_sub(a.v, b.v)) };
}
