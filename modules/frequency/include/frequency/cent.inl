#ifdef _CLANGD
#pragma once
#include "cent.h"
#endif

static inline void mel_cent_view(mpfr_t out, const Mel_Cent *c)
{
  mel_real_view(out, &c->v);
}

MEL_OVERLOADABLE static inline Mel_Cent mel_cent(double value)
{
  return (Mel_Cent){ .v = mel_real(value) };
}

MEL_OVERLOADABLE static inline Mel_Cent mel_cent(mpfr_srcptr value)
{
  return (Mel_Cent){ .v = mel_real(value) };
}

static inline double mel_cent_to_double(Mel_Cent c)
{
  return mel_real_to_double(c.v);
}

static inline Mel_Cent mel_cent_add(Mel_Cent a, Mel_Cent b)
{
  return (Mel_Cent){ .v = mel_real_add(a.v, b.v) };
}

static inline Mel_Cent mel_cent_sub(Mel_Cent a, Mel_Cent b)
{
  return (Mel_Cent){ .v = mel_real_sub(a.v, b.v) };
}

static inline Mel_Cent mel_cent_neg(Mel_Cent c)
{
  return (Mel_Cent){ .v = mel_real_neg(c.v) };
}

MEL_OVERLOADABLE static inline Mel_Cent mel_cent_mul(Mel_Cent c, mpfr_srcptr scalar)
{
  return (Mel_Cent){ .v = mel_real_mul(c.v, scalar) };
}

MEL_OVERLOADABLE static inline Mel_Cent mel_cent_mul(Mel_Cent c, double scalar)
{
  return (Mel_Cent){ .v = mel_real_mul(c.v, scalar) };
}

MEL_OVERLOADABLE static inline Mel_Cent mel_cent_div(Mel_Cent c, mpfr_srcptr scalar)
{
  return (Mel_Cent){ .v = mel_real_div(c.v, scalar) };
}

MEL_OVERLOADABLE static inline Mel_Cent mel_cent_div(Mel_Cent c, double scalar)
{
  return (Mel_Cent){ .v = mel_real_div(c.v, scalar) };
}

static inline Mel_Cent mel_cent_abs(Mel_Cent c)
{
  return (Mel_Cent){ .v = mel_real_abs(c.v) };
}

static inline Mel_Cent mel_cent_min(Mel_Cent a, Mel_Cent b)
{
  return (Mel_Cent){ .v = mel_real_min(a.v, b.v) };
}

static inline Mel_Cent mel_cent_max(Mel_Cent a, Mel_Cent b)
{
  return (Mel_Cent){ .v = mel_real_max(a.v, b.v) };
}

static inline uint8_t mel_cent_cmp(Mel_Cent a, Mel_Cent b)
{
  return mel_real_cmp(a.v, b.v);
}

static inline uint8_t mel_cent_eq(Mel_Cent a, Mel_Cent b)
{
  return mel_real_eq(a.v, b.v);
}

static inline uint8_t mel_cent_near(Mel_Cent a, Mel_Cent b, mpfr_srcptr tolerance)
{
  return mel_real_near(a.v, b.v, tolerance);
}

static inline uint8_t mel_cent_is_zero(Mel_Cent c)
{
  return mel_real_is_zero(c.v);
}
