#pragma once

#include <stdint.h>
#include <mpfr.h>
#include "../time.frequency/frequency.h"

typedef struct Mel_Cent Mel_Cent;

struct Mel_Cent
{
  mpfr_t value;
};

void mel_cent_free(Mel_Cent* c);

static inline void mel_cent_cleanup(Mel_Cent* c) { mel_cent_free(c); }

#define Mel_Cent_AUTO __attribute__((cleanup(mel_cent_cleanup))) Mel_Cent

Mel_Cent __attribute__((overloadable)) mel_cent(double value);
Mel_Cent __attribute__((overloadable)) mel_cent(mpfr_srcptr value);

double mel_cent_to_double(Mel_Cent c);

void mel_cent_from_ratio(mpfr_ptr out, mpfr_srcptr ratio);
Mel_Cent mel_cent_from_ratio_c(mpfr_srcptr ratio);

void mel_cent_to_ratio(mpfr_ptr out, Mel_Cent c);
Mel_Cent mel_cent_to_ratio_c(Mel_Cent c);

void mel_cent_from_freqs(mpfr_ptr out, const Mel_Hz* a, const Mel_Hz* b);
Mel_Cent mel_cent_from_freqs_c(const Mel_Hz* a, const Mel_Hz* b);

Mel_Cent mel_cent_add(Mel_Cent a, Mel_Cent b);
Mel_Cent mel_cent_sub(Mel_Cent a, Mel_Cent b);
Mel_Cent mel_cent_neg(Mel_Cent c);

Mel_Cent __attribute__((overloadable)) mel_cent_mul(Mel_Cent c, mpfr_srcptr scalar);
Mel_Cent __attribute__((overloadable)) mel_cent_mul(Mel_Cent c, double scalar);

Mel_Cent __attribute__((overloadable)) mel_cent_div(Mel_Cent c, mpfr_srcptr scalar);
Mel_Cent __attribute__((overloadable)) mel_cent_div(Mel_Cent c, double scalar);

Mel_Cent mel_cent_abs(Mel_Cent c);

Mel_Cent mel_cent_min(Mel_Cent a, Mel_Cent b);
Mel_Cent mel_cent_max(Mel_Cent a, Mel_Cent b);

uint8_t mel_cent_cmp(Mel_Cent a, Mel_Cent b);
uint8_t mel_cent_eq(Mel_Cent a, Mel_Cent b);
uint8_t mel_cent_near(Mel_Cent a, Mel_Cent b, mpfr_srcptr tolerance);
uint8_t mel_cent_is_zero(Mel_Cent c);
