#include "cent.h"
#include "../time.frequency/frequency.h"

static inline void mel_cent_flags_restore(const mpfr_flags_t* saved)
{
  mpfr_flags_restore(*saved, MPFR_FLAGS_ALL);
}

#define MEL_CENT_PROTECT_FLAGS \
  __attribute__((cleanup(mel_cent_flags_restore))) \
  mpfr_flags_t _mel_cent_saved_flags = mpfr_flags_save()

static inline void mel_cent_bind_out(mpfr_t out, Mel_Cent* r)
{
  mpfr_custom_init(r->limbs, MEL_CENT_PRECISION);
  mpfr_custom_init_set(out, MPFR_REGULAR_KIND, 0, MEL_CENT_PRECISION, r->limbs);
}

static inline void mel_cent_store(Mel_Cent* r, mpfr_srcptr v)
{
  r->kind = mpfr_custom_get_kind(v);
  r->exp  = mpfr_custom_get_exp(v);
}

static inline void mel_cent_scratch(mpfr_t out, mp_limb_t* limbs)
{
  mpfr_custom_init(limbs, MEL_CENT_PRECISION);
  mpfr_custom_init_set(out, MPFR_REGULAR_KIND, 0, MEL_CENT_PRECISION, limbs);
}

Mel_Cent __attribute__((overloadable)) mel_cent(double value)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent c;
  mpfr_t v;
  mel_cent_bind_out(v, &c);
  mpfr_set_d(v, value, MPFR_RNDN);
  mel_cent_store(&c, v);
  return c;
}

Mel_Cent __attribute__((overloadable)) mel_cent(mpfr_srcptr value)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent c;
  mpfr_t v;
  mel_cent_bind_out(v, &c);
  mpfr_set(v, value, MPFR_RNDN);
  mel_cent_store(&c, v);
  return c;
}

double mel_cent_to_double(Mel_Cent c)
{
  MEL_CENT_PROTECT_FLAGS;
  mpfr_t v;
  mel_cent_view(v, &c);
  return mpfr_get_d(v, MPFR_RNDN);
}

void mel_cent_from_ratio(mpfr_ptr out, mpfr_srcptr ratio)
{
  mpfr_log2(out, ratio, MPFR_RNDN);
  mpfr_mul_ui(out, out, 1200, MPFR_RNDN);
}

Mel_Cent mel_cent_from_ratio_c(mpfr_srcptr ratio)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent c;
  mpfr_t v;
  mel_cent_bind_out(v, &c);
  mel_cent_from_ratio(v, ratio);
  mel_cent_store(&c, v);
  return c;
}

void mel_cent_to_ratio(mpfr_ptr out, Mel_Cent c)
{
  mpfr_t vc;
  mel_cent_view(vc, &c);
  mpfr_div_ui(out, vc, 1200, MPFR_RNDN);
  mpfr_exp2(out, out, MPFR_RNDN);
}

Mel_Cent mel_cent_to_ratio_c(Mel_Cent c)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t vr;
  mel_cent_bind_out(vr, &r);
  mel_cent_to_ratio(vr, c);
  mel_cent_store(&r, vr);
  return r;
}

void mel_cent_from_freqs(mpfr_ptr out, const Mel_Hz* a, const Mel_Hz* b)
{
  mpfr_t va, vb, ratio;
  mp_limb_t ratio_limbs[MEL_CENT_LIMBS];
  mel_freq_view(va, a);
  mel_freq_view(vb, b);
  mel_cent_scratch(ratio, ratio_limbs);
  mpfr_div(ratio, va, vb, MPFR_RNDN);
  mel_cent_from_ratio(out, ratio);
}

Mel_Cent mel_cent_from_freqs_c(const Mel_Hz* a, const Mel_Hz* b)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent c;
  mpfr_t v;
  mel_cent_bind_out(v, &c);
  mel_cent_from_freqs(v, a, b);
  mel_cent_store(&c, v);
  return c;
}

Mel_Cent mel_cent_add(Mel_Cent a, Mel_Cent b)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t va, vb, vr;
  mel_cent_view(va, &a);
  mel_cent_view(vb, &b);
  mel_cent_bind_out(vr, &r);
  mpfr_add(vr, va, vb, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent mel_cent_sub(Mel_Cent a, Mel_Cent b)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t va, vb, vr;
  mel_cent_view(va, &a);
  mel_cent_view(vb, &b);
  mel_cent_bind_out(vr, &r);
  mpfr_sub(vr, va, vb, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent mel_cent_neg(Mel_Cent c)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t vc, vr;
  mel_cent_view(vc, &c);
  mel_cent_bind_out(vr, &r);
  mpfr_neg(vr, vc, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent __attribute__((overloadable)) mel_cent_mul(Mel_Cent c, mpfr_srcptr scalar)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t vc, vr;
  mel_cent_view(vc, &c);
  mel_cent_bind_out(vr, &r);
  mpfr_mul(vr, vc, scalar, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent __attribute__((overloadable)) mel_cent_mul(Mel_Cent c, double scalar)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t vc, vr;
  mel_cent_view(vc, &c);
  mel_cent_bind_out(vr, &r);
  mpfr_mul_d(vr, vc, scalar, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent __attribute__((overloadable)) mel_cent_div(Mel_Cent c, mpfr_srcptr scalar)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t vc, vr;
  mel_cent_view(vc, &c);
  mel_cent_bind_out(vr, &r);
  mpfr_div(vr, vc, scalar, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent __attribute__((overloadable)) mel_cent_div(Mel_Cent c, double scalar)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t vc, vr;
  mel_cent_view(vc, &c);
  mel_cent_bind_out(vr, &r);
  mpfr_div_d(vr, vc, scalar, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent mel_cent_abs(Mel_Cent c)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t vc, vr;
  mel_cent_view(vc, &c);
  mel_cent_bind_out(vr, &r);
  mpfr_abs(vr, vc, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent mel_cent_min(Mel_Cent a, Mel_Cent b)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t va, vb, vr;
  mel_cent_view(va, &a);
  mel_cent_view(vb, &b);
  mel_cent_bind_out(vr, &r);
  mpfr_min(vr, va, vb, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

Mel_Cent mel_cent_max(Mel_Cent a, Mel_Cent b)
{
  MEL_CENT_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t va, vb, vr;
  mel_cent_view(va, &a);
  mel_cent_view(vb, &b);
  mel_cent_bind_out(vr, &r);
  mpfr_max(vr, va, vb, MPFR_RNDN);
  mel_cent_store(&r, vr);
  return r;
}

uint8_t mel_cent_cmp(Mel_Cent a, Mel_Cent b)
{
  MEL_CENT_PROTECT_FLAGS;
  mpfr_t va, vb;
  mel_cent_view(va, &a);
  mel_cent_view(vb, &b);
  int cmp = mpfr_cmp(va, vb);
  if (cmp < 0) return 0;
  if (cmp > 0) return 2;
  return 1;
}

uint8_t mel_cent_eq(Mel_Cent a, Mel_Cent b)
{
  MEL_CENT_PROTECT_FLAGS;
  mpfr_t va, vb;
  mel_cent_view(va, &a);
  mel_cent_view(vb, &b);
  return mpfr_equal_p(va, vb) ? 1 : 0;
}

uint8_t mel_cent_near(Mel_Cent a, Mel_Cent b, mpfr_srcptr tolerance)
{
  MEL_CENT_PROTECT_FLAGS;
  mpfr_t va, vb, diff;
  mp_limb_t diff_limbs[MEL_CENT_LIMBS];
  mel_cent_view(va, &a);
  mel_cent_view(vb, &b);
  mel_cent_scratch(diff, diff_limbs);
  mpfr_sub(diff, va, vb, MPFR_RNDN);
  mpfr_abs(diff, diff, MPFR_RNDN);
  return mpfr_lessequal_p(diff, tolerance) ? 1 : 0;
}

uint8_t mel_cent_is_zero(Mel_Cent c)
{
  MEL_CENT_PROTECT_FLAGS;
  mpfr_t vc;
  mel_cent_view(vc, &c);
  return mpfr_zero_p(vc) ? 1 : 0;
}
