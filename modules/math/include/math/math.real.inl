#ifdef _CLANGD
#pragma once
#include "math.real.h"
#endif

static inline void mel_real_view(mpfr_t out, const Mel_Real *r)
{
  mp_limb_t *limbs = (mp_limb_t *)r->limbs;
  mpfr_custom_init(limbs, MEL_REAL_PRECISION);
  mpfr_custom_init_set(out, r->kind, r->exp, MEL_REAL_PRECISION, limbs);
}

static inline void mel_real_bind_out(mpfr_t out, Mel_Real *r)
{
  mpfr_custom_init(r->limbs, MEL_REAL_PRECISION);
  mpfr_custom_init_set(out, MPFR_REGULAR_KIND, 0, MEL_REAL_PRECISION, r->limbs);
}

static inline void mel_real_store(Mel_Real *r, mpfr_srcptr v)
{
  r->kind = mpfr_custom_get_kind(v);
  r->exp  = mpfr_custom_get_exp(v);
}

static inline void mel_real_scratch(mpfr_t out, mp_limb_t *limbs)
{
  mpfr_custom_init(limbs, MEL_REAL_PRECISION);
  mpfr_custom_init_set(out, MPFR_REGULAR_KIND, 0, MEL_REAL_PRECISION, limbs);
}

static inline void mel_real_flags_restore(const mpfr_flags_t *saved)
{
  mpfr_flags_restore(*saved, MPFR_FLAGS_ALL);
}
