#pragma once

#include <stdint.h>
#include <mpfr.h>

typedef struct Mel_Hz Mel_Hz;

struct Mel_Hz
{
  mpfr_t value;
};

void mel_freq_free(Mel_Hz *f);

static inline void mel_freq_cleanup(Mel_Hz *f) { mel_freq_free(f); }

#define Mel_Hz_AUTO __attribute__((cleanup(mel_freq_cleanup))) Mel_Hz

Mel_Hz __attribute__((overloadable)) mel_freq(double value);
Mel_Hz __attribute__((overloadable)) mel_freq(mpfr_srcptr value);
Mel_Hz __attribute__((overloadable)) mel_freq(mpq_srcptr value);
Mel_Hz __attribute__((overloadable)) mel_freq(unsigned num, unsigned den);

double mel_freq_to_double(Mel_Hz f);

Mel_Hz mel_freq_add(Mel_Hz a, Mel_Hz b);
Mel_Hz mel_freq_sub(Mel_Hz a, Mel_Hz b);
Mel_Hz mel_freq_neg(Mel_Hz f);

Mel_Hz __attribute__((overloadable)) mel_freq_mul(Mel_Hz a, mpfr_srcptr b);
Mel_Hz __attribute__((overloadable)) mel_freq_mul(Mel_Hz a, double b);

Mel_Hz __attribute__((overloadable)) mel_freq_div(Mel_Hz a, mpfr_srcptr b);
Mel_Hz __attribute__((overloadable)) mel_freq_div(Mel_Hz a, double b);

double __attribute__((overloadable)) mel_freq_ratio(Mel_Hz a, Mel_Hz b);
void   __attribute__((overloadable)) mel_freq_ratio(mpfr_ptr out, Mel_Hz a, Mel_Hz b);

Mel_Hz mel_freq_transpose(Mel_Hz f, mpq_srcptr ratio);
Mel_Hz mel_freq_transpose_cents(Mel_Hz f, mpfr_srcptr cents);
Mel_Hz mel_freq_transpose_semitones(Mel_Hz f, mpfr_srcptr semitones);
Mel_Hz mel_freq_octave_up(Mel_Hz f);
Mel_Hz mel_freq_octave_down(Mel_Hz f);

Mel_Hz mel_freq_harmonic(Mel_Hz f, unsigned n);
Mel_Hz mel_freq_midpoint(Mel_Hz a, Mel_Hz b);

Mel_Hz mel_freq_mod(Mel_Hz a, Mel_Hz b);
Mel_Hz mel_freq_floordiv(Mel_Hz a, Mel_Hz b);

Mel_Hz mel_freq_abs(Mel_Hz a);

Mel_Hz mel_freq_min(Mel_Hz a, Mel_Hz b);
Mel_Hz mel_freq_max(Mel_Hz a, Mel_Hz b);

uint8_t mel_freq_cmp(Mel_Hz a, Mel_Hz b);
uint8_t mel_freq_eq(Mel_Hz a, Mel_Hz b);
uint8_t mel_freq_near(Mel_Hz a, Mel_Hz b, mpfr_srcptr tolerance);
uint8_t mel_freq_is_zero(Mel_Hz f);

Mel_Hz mel_freq_beat(Mel_Hz a, Mel_Hz b);
