#include "frequency.h"
#include <stdlib.h>

#define MEL_FREQ_PRECISION 256

static mpfr_prec_t mel_freq_precision(void)
{
  return MEL_FREQ_PRECISION;
}

void mel_freq_free(Mel_Hz *f)
{
  if (f) mpfr_clear(f->value);
}

Mel_Hz __attribute__((overloadable)) mel_freq(double value)
{
  Mel_Hz f;
  mpfr_init2(f.value, mel_freq_precision());
  mpfr_set_d(f.value, value, MPFR_RNDN);
  return f;
}

Mel_Hz __attribute__((overloadable)) mel_freq(mpfr_srcptr value)
{
  Mel_Hz f;
  mpfr_init2(f.value, mel_freq_precision());
  mpfr_set(f.value, value, MPFR_RNDN);
  return f;
}

Mel_Hz __attribute__((overloadable)) mel_freq(mpq_srcptr value)
{
  Mel_Hz f;
  mpfr_init2(f.value, mel_freq_precision());
  mpfr_set_q(f.value, value, MPFR_RNDN);
  return f;
}

Mel_Hz __attribute__((overloadable)) mel_freq(unsigned num, unsigned den)
{
  Mel_Hz f;
  mpfr_init2(f.value, mel_freq_precision());
  mpfr_set_ui(f.value, num, MPFR_RNDN);
  mpfr_div_ui(f.value, f.value, den, MPFR_RNDN);
  return f;
}

double mel_freq_to_double(Mel_Hz f)
{
  return mpfr_get_d(f.value, MPFR_RNDN);
}

Mel_Hz mel_freq_add(Mel_Hz a, Mel_Hz b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_add(r.value, a.value, b.value, MPFR_RNDN);
  return r;
}

Mel_Hz mel_freq_sub(Mel_Hz a, Mel_Hz b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_sub(r.value, a.value, b.value, MPFR_RNDN);
  return r;
}

Mel_Hz mel_freq_neg(Mel_Hz f)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_neg(r.value, f.value, MPFR_RNDN);
  return r;
}

Mel_Hz __attribute__((overloadable)) mel_freq_mul(Mel_Hz a, mpfr_srcptr b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_mul(r.value, a.value, b, MPFR_RNDN);
  return r;
}

Mel_Hz __attribute__((overloadable)) mel_freq_mul(Mel_Hz a, double b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_mul_d(r.value, a.value, b, MPFR_RNDN);
  return r;
}

Mel_Hz __attribute__((overloadable)) mel_freq_div(Mel_Hz a, mpfr_srcptr b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_div(r.value, a.value, b, MPFR_RNDN);
  return r;
}

Mel_Hz __attribute__((overloadable)) mel_freq_div(Mel_Hz a, double b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_div_d(r.value, a.value, b, MPFR_RNDN);
  return r;
}

double __attribute__((overloadable)) mel_freq_ratio(Mel_Hz a, Mel_Hz b)
{
  mpfr_t r;
  mpfr_init2(r, mel_freq_precision());
  mpfr_div(r, a.value, b.value, MPFR_RNDN);
  double result = mpfr_get_d(r, MPFR_RNDN);
  mpfr_clear(r);
  return result;
}

void __attribute__((overloadable)) mel_freq_ratio(mpfr_ptr out, Mel_Hz a, Mel_Hz b)
{
  mpfr_div(out, a.value, b.value, MPFR_RNDN);
}

void mel_freq_cents(mpfr_ptr out, Mel_Hz a, Mel_Hz b)
{
  mpfr_t ratio;
  mpfr_init2(ratio, mel_freq_precision());
  mpfr_div(ratio, a.value, b.value, MPFR_RNDN);
  mpfr_log2(out, ratio, MPFR_RNDN);
  mpfr_mul_ui(out, out, 1200, MPFR_RNDN);
  mpfr_clear(ratio);
}

Mel_Hz mel_freq_transpose(Mel_Hz f, mpq_srcptr ratio)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());

  mpfr_t factor;
  mpfr_init2(factor, mel_freq_precision());
  mpfr_set_q(factor, ratio, MPFR_RNDN);
  mpfr_mul(r.value, f.value, factor, MPFR_RNDN);

  mpfr_clear(factor);
  return r;
}

Mel_Hz mel_freq_transpose_cents(Mel_Hz f, mpfr_srcptr cents)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());

  mpfr_t factor;
  mpfr_init2(factor, mel_freq_precision());
  mpfr_div_ui(factor, cents, 1200, MPFR_RNDN);
  mpfr_exp2(factor, factor, MPFR_RNDN);
  mpfr_mul(r.value, f.value, factor, MPFR_RNDN);

  mpfr_clear(factor);
  return r;
}

Mel_Hz mel_freq_transpose_semitones(Mel_Hz f, mpfr_srcptr semitones)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());

  mpfr_t factor;
  mpfr_init2(factor, mel_freq_precision());
  mpfr_div_ui(factor, semitones, 12, MPFR_RNDN);
  mpfr_exp2(factor, factor, MPFR_RNDN);
  mpfr_mul(r.value, f.value, factor, MPFR_RNDN);

  mpfr_clear(factor);
  return r;
}

Mel_Hz mel_freq_octave_up(Mel_Hz f)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_mul_ui(r.value, f.value, 2, MPFR_RNDN);
  return r;
}

Mel_Hz mel_freq_octave_down(Mel_Hz f)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_div_ui(r.value, f.value, 2, MPFR_RNDN);
  return r;
}

Mel_Hz mel_freq_harmonic(Mel_Hz f, unsigned n)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_mul_ui(r.value, f.value, n, MPFR_RNDN);
  return r;
}

Mel_Hz mel_freq_midpoint(Mel_Hz a, Mel_Hz b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_add(r.value, a.value, b.value, MPFR_RNDN);
  mpfr_div_ui(r.value, r.value, 2, MPFR_RNDN);
  return r;
}

Mel_Hz mel_freq_mod(Mel_Hz a, Mel_Hz b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());

  mpfr_t q;
  mpfr_init2(q, mel_freq_precision());
  mpfr_div(q, a.value, b.value, MPFR_RNDN);
  mpfr_trunc(q, q);
  mpfr_mul(q, q, b.value, MPFR_RNDN);
  mpfr_sub(r.value, a.value, q, MPFR_RNDN);

  mpfr_clear(q);
  return r;
}

Mel_Hz mel_freq_floordiv(Mel_Hz a, Mel_Hz b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_div(r.value, a.value, b.value, MPFR_RNDN);
  mpfr_floor(r.value, r.value);
  return r;
}

Mel_Hz mel_freq_abs(Mel_Hz a)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_abs(r.value, a.value, MPFR_RNDN);
  return r;
}

Mel_Hz mel_freq_min(Mel_Hz a, Mel_Hz b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_min(r.value, a.value, b.value, MPFR_RNDN);
  return r;
}

Mel_Hz mel_freq_max(Mel_Hz a, Mel_Hz b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_max(r.value, a.value, b.value, MPFR_RNDN);
  return r;
}

uint8_t mel_freq_cmp(Mel_Hz a, Mel_Hz b)
{
  int cmp = mpfr_cmp(a.value, b.value);
  if (cmp < 0) return 0;
  if (cmp > 0) return 2;
  return 1;
}

uint8_t mel_freq_eq(Mel_Hz a, Mel_Hz b)
{
  return mpfr_equal_p(a.value, b.value) ? 1 : 0;
}

uint8_t mel_freq_near(Mel_Hz a, Mel_Hz b, mpfr_srcptr tolerance)
{
  mpfr_t diff;
  mpfr_init2(diff, mel_freq_precision());
  mpfr_sub(diff, a.value, b.value, MPFR_RNDN);
  mpfr_abs(diff, diff, MPFR_RNDN);
  int result = mpfr_lessequal_p(diff, tolerance);
  mpfr_clear(diff);
  return result ? 1 : 0;
}

uint8_t mel_freq_is_zero(Mel_Hz f)
{
  return mpfr_zero_p(f.value) ? 1 : 0;
}

Mel_Hz mel_freq_beat(Mel_Hz a, Mel_Hz b)
{
  Mel_Hz r;
  mpfr_init2(r.value, mel_freq_precision());
  mpfr_sub(r.value, a.value, b.value, MPFR_RNDN);
  mpfr_abs(r.value, r.value, MPFR_RNDN);
  return r;
}
