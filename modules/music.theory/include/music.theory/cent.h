#pragma once

#include <core/compiler.h>
#include <time.frequency/frequency.h>

#include <stdint.h>
#include <mpfr.h>
#include <gmp.h>

#define MEL_CENT_PRECISION 256
#define MEL_CENT_LIMBS ((MEL_CENT_PRECISION + GMP_NUMB_BITS - 1) / GMP_NUMB_BITS)

typedef struct Mel_Cent Mel_Cent;

struct Mel_Cent
{
  int        kind;
  mpfr_exp_t exp;
  mp_limb_t  limbs[MEL_CENT_LIMBS];
};

static inline void mel_cent_view(mpfr_t out, const Mel_Cent* c)
{
  mp_limb_t* limbs = (mp_limb_t*)c->limbs;
  mpfr_custom_init(limbs, MEL_CENT_PRECISION);
  mpfr_custom_init_set(out, c->kind, c->exp, MEL_CENT_PRECISION, limbs);
}

Mel_Cent MEL_OVERLOADABLE MEL_CONST mel_cent(double value);
Mel_Cent MEL_OVERLOADABLE MEL_PURE  mel_cent(mpfr_srcptr value);

MEL_CONST double mel_cent_to_double(Mel_Cent c);

void mel_cent_from_ratio(mpfr_ptr out, mpfr_srcptr ratio);
MEL_PURE Mel_Cent mel_cent_from_ratio_c(mpfr_srcptr ratio);

void mel_cent_to_ratio(mpfr_ptr out, Mel_Cent c);
MEL_CONST Mel_Cent mel_cent_to_ratio_c(Mel_Cent c);

void mel_cent_from_freqs(mpfr_ptr out, const Mel_Hz* a, const Mel_Hz* b);
MEL_PURE Mel_Cent mel_cent_from_freqs_c(const Mel_Hz* a, const Mel_Hz* b);

MEL_CONST Mel_Cent mel_cent_add(Mel_Cent a, Mel_Cent b);
MEL_CONST Mel_Cent mel_cent_sub(Mel_Cent a, Mel_Cent b);
MEL_CONST Mel_Cent mel_cent_neg(Mel_Cent c);

Mel_Cent MEL_OVERLOADABLE MEL_PURE  mel_cent_mul(Mel_Cent c, mpfr_srcptr scalar);
Mel_Cent MEL_OVERLOADABLE MEL_CONST mel_cent_mul(Mel_Cent c, double scalar);

Mel_Cent MEL_OVERLOADABLE MEL_PURE  mel_cent_div(Mel_Cent c, mpfr_srcptr scalar);
Mel_Cent MEL_OVERLOADABLE MEL_CONST mel_cent_div(Mel_Cent c, double scalar);

MEL_CONST Mel_Cent mel_cent_abs(Mel_Cent c);

MEL_CONST Mel_Cent mel_cent_min(Mel_Cent a, Mel_Cent b);
MEL_CONST Mel_Cent mel_cent_max(Mel_Cent a, Mel_Cent b);

MEL_CONST uint8_t mel_cent_cmp(Mel_Cent a, Mel_Cent b);
MEL_CONST uint8_t mel_cent_eq(Mel_Cent a, Mel_Cent b);
MEL_PURE  uint8_t mel_cent_near(Mel_Cent a, Mel_Cent b, mpfr_srcptr tolerance);
MEL_CONST uint8_t mel_cent_is_zero(Mel_Cent c);
