#include "cent.h"
#include "../time.frequency/frequency.h"
#include <stdlib.h>

#define MEL_CENT_PRECISION 256

static mpfr_prec_t mel_cent_precision(void)
{
  return MEL_CENT_PRECISION;
}

void mel_cent_free(Mel_Cent* c)
{
  if (c) mpfr_clear(c->value);
}

Mel_Cent __attribute__((overloadable)) mel_cent(double value)
{
  Mel_Cent c;
  mpfr_init2(c.value, mel_cent_precision());
  mpfr_set_d(c.value, value, MPFR_RNDN);
  return c;
}

Mel_Cent __attribute__((overloadable)) mel_cent(mpfr_srcptr value)
{
  Mel_Cent c;
  mpfr_init2(c.value, mel_cent_precision());
  mpfr_set(c.value, value, MPFR_RNDN);
  return c;
}

double mel_cent_to_double(Mel_Cent c)
{
  return mpfr_get_d(c.value, MPFR_RNDN);
}

void mel_cent_from_ratio(mpfr_ptr out, mpfr_srcptr ratio)
{
  mpfr_log2(out, ratio, MPFR_RNDN);
  mpfr_mul_ui(out, out, 1200, MPFR_RNDN);
}

Mel_Cent mel_cent_from_ratio_c(mpfr_srcptr ratio)
{
  Mel_Cent c;
  mpfr_init2(c.value, mel_cent_precision());
  mel_cent_from_ratio(c.value, ratio);
  return c;
}

void mel_cent_to_ratio(mpfr_ptr out, Mel_Cent c)
{
  mpfr_div_ui(out, c.value, 1200, MPFR_RNDN);
  mpfr_exp2(out, out, MPFR_RNDN);
}

Mel_Cent mel_cent_to_ratio_c(Mel_Cent c)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mel_cent_to_ratio(r.value, c);
  return r;
}

void mel_cent_from_freqs(mpfr_ptr out, const Mel_Hz* a, const Mel_Hz* b)
{
  mpfr_t ratio;
  mpfr_init2(ratio, mel_cent_precision());
  mpfr_div(ratio, a->value, b->value, MPFR_RNDN);
  mel_cent_from_ratio(out, ratio);
  mpfr_clear(ratio);
}

Mel_Cent mel_cent_from_freqs_c(const Mel_Hz* a, const Mel_Hz* b)
{
  Mel_Cent c;
  mpfr_init2(c.value, mel_cent_precision());
  mel_cent_from_freqs(c.value, a, b);
  return c;
}

Mel_Cent mel_cent_add(Mel_Cent a, Mel_Cent b)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_add(r.value, a.value, b.value, MPFR_RNDN);
  return r;
}

Mel_Cent mel_cent_sub(Mel_Cent a, Mel_Cent b)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_sub(r.value, a.value, b.value, MPFR_RNDN);
  return r;
}

Mel_Cent mel_cent_neg(Mel_Cent c)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_neg(r.value, c.value, MPFR_RNDN);
  return r;
}

Mel_Cent __attribute__((overloadable)) mel_cent_mul(Mel_Cent c, mpfr_srcptr scalar)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_mul(r.value, c.value, scalar, MPFR_RNDN);
  return r;
}

Mel_Cent __attribute__((overloadable)) mel_cent_mul(Mel_Cent c, double scalar)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_mul_d(r.value, c.value, scalar, MPFR_RNDN);
  return r;
}

Mel_Cent __attribute__((overloadable)) mel_cent_div(Mel_Cent c, mpfr_srcptr scalar)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_div(r.value, c.value, scalar, MPFR_RNDN);
  return r;
}

Mel_Cent __attribute__((overloadable)) mel_cent_div(Mel_Cent c, double scalar)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_div_d(r.value, c.value, scalar, MPFR_RNDN);
  return r;
}

Mel_Cent mel_cent_abs(Mel_Cent c)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_abs(r.value, c.value, MPFR_RNDN);
  return r;
}

Mel_Cent mel_cent_min(Mel_Cent a, Mel_Cent b)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_min(r.value, a.value, b.value, MPFR_RNDN);
  return r;
}

Mel_Cent mel_cent_max(Mel_Cent a, Mel_Cent b)
{
  Mel_Cent r;
  mpfr_init2(r.value, mel_cent_precision());
  mpfr_max(r.value, a.value, b.value, MPFR_RNDN);
  return r;
}

uint8_t mel_cent_cmp(Mel_Cent a, Mel_Cent b)
{
  int cmp = mpfr_cmp(a.value, b.value);
  if (cmp < 0) return 0;
  if (cmp > 0) return 2;
  return 1;
}

uint8_t mel_cent_eq(Mel_Cent a, Mel_Cent b)
{
  return mpfr_equal_p(a.value, b.value) ? 1 : 0;
}

uint8_t mel_cent_near(Mel_Cent a, Mel_Cent b, mpfr_srcptr tolerance)
{
  mpfr_t diff;
  mpfr_init2(diff, mel_cent_precision());
  mpfr_sub(diff, a.value, b.value, MPFR_RNDN);
  mpfr_abs(diff, diff, MPFR_RNDN);
  int result = mpfr_lessequal_p(diff, tolerance);
  mpfr_clear(diff);
  return result ? 1 : 0;
}

uint8_t mel_cent_is_zero(Mel_Cent c)
{
  return mpfr_zero_p(c.value) ? 1 : 0;
}
