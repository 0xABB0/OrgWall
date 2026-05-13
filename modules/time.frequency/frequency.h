#pragma once

#include <stdint.h>
#include <mpfr.h>
#include <gmp.h>

#define MEL_FREQ_PRECISION 256
#define MEL_FREQ_LIMBS ((MEL_FREQ_PRECISION + GMP_NUMB_BITS - 1) / GMP_NUMB_BITS)

typedef struct Mel_Hz Mel_Hz;

struct Mel_Hz
{
  int        kind;
  mpfr_exp_t exp;
  mp_limb_t  limbs[MEL_FREQ_LIMBS];
};

static inline void mel_freq_view(mpfr_t out, const Mel_Hz *f)
{
  mp_limb_t *limbs = (mp_limb_t *)f->limbs;
  mpfr_custom_init(limbs, MEL_FREQ_PRECISION);
  mpfr_custom_init_set(out, f->kind, f->exp, MEL_FREQ_PRECISION, limbs);
}

[[nodiscard]] Mel_Hz __attribute__((overloadable)) mel_freq(double value);
[[nodiscard]] Mel_Hz __attribute__((overloadable)) mel_freq(mpfr_srcptr value);
[[nodiscard]] Mel_Hz __attribute__((overloadable)) mel_freq(mpq_srcptr value);
[[nodiscard]] Mel_Hz __attribute__((overloadable)) mel_freq(unsigned num, unsigned den);

[[nodiscard]] double mel_freq_to_double(Mel_Hz f);

[[nodiscard]] Mel_Hz mel_freq_add(Mel_Hz a, Mel_Hz b);
[[nodiscard]] Mel_Hz mel_freq_sub(Mel_Hz a, Mel_Hz b);
[[nodiscard]] Mel_Hz mel_freq_neg(Mel_Hz f);

[[nodiscard]] Mel_Hz __attribute__((overloadable)) mel_freq_mul(Mel_Hz a, mpfr_srcptr b);
[[nodiscard]] Mel_Hz __attribute__((overloadable)) mel_freq_mul(Mel_Hz a, double b);

[[nodiscard]] Mel_Hz __attribute__((overloadable)) mel_freq_div(Mel_Hz a, mpfr_srcptr b);
[[nodiscard]] Mel_Hz __attribute__((overloadable)) mel_freq_div(Mel_Hz a, double b);

[[nodiscard]] double __attribute__((overloadable)) mel_freq_ratio(Mel_Hz a, Mel_Hz b);
void   __attribute__((overloadable)) mel_freq_ratio(mpfr_ptr out, Mel_Hz a, Mel_Hz b);

[[nodiscard]] Mel_Hz mel_freq_transpose(Mel_Hz f, mpq_srcptr ratio);
[[nodiscard]] Mel_Hz mel_freq_transpose_cents(Mel_Hz f, mpfr_srcptr cents);
[[nodiscard]] Mel_Hz mel_freq_transpose_semitones(Mel_Hz f, mpfr_srcptr semitones);
[[nodiscard]] Mel_Hz mel_freq_octave_up(Mel_Hz f);
[[nodiscard]] Mel_Hz mel_freq_octave_down(Mel_Hz f);

[[nodiscard]] Mel_Hz mel_freq_harmonic(Mel_Hz f, unsigned n);
[[nodiscard]] Mel_Hz mel_freq_midpoint(Mel_Hz a, Mel_Hz b);

[[nodiscard]] Mel_Hz mel_freq_mod(Mel_Hz a, Mel_Hz b);
[[nodiscard]] Mel_Hz mel_freq_floordiv(Mel_Hz a, Mel_Hz b);

[[nodiscard]] Mel_Hz mel_freq_abs(Mel_Hz a);

[[nodiscard]] Mel_Hz mel_freq_min(Mel_Hz a, Mel_Hz b);
[[nodiscard]] Mel_Hz mel_freq_max(Mel_Hz a, Mel_Hz b);

[[nodiscard]] uint8_t mel_freq_cmp(Mel_Hz a, Mel_Hz b);
[[nodiscard]] uint8_t mel_freq_eq(Mel_Hz a, Mel_Hz b);
[[nodiscard]] uint8_t mel_freq_near(Mel_Hz a, Mel_Hz b, mpfr_srcptr tolerance);
[[nodiscard]] uint8_t mel_freq_is_zero(Mel_Hz f);

[[nodiscard]] Mel_Hz mel_freq_beat(Mel_Hz a, Mel_Hz b);
