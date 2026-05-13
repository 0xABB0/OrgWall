#include "frequency.h"
#include <core/compiler.h>

static inline void mel_freq_flags_restore(const mpfr_flags_t *saved)
{
  mpfr_flags_restore(*saved, MPFR_FLAGS_ALL);
}

#define MEL_FREQ_PROTECT_FLAGS \
  MEL_CLEANUP(mel_freq_flags_restore) \
  mpfr_flags_t _mel_freq_saved_flags = mpfr_flags_save()

static inline void mel_freq_bind_out(mpfr_t out, Mel_Hz *r)
{
  mpfr_custom_init(r->limbs, MEL_FREQ_PRECISION);
  mpfr_custom_init_set(out, MPFR_REGULAR_KIND, 0, MEL_FREQ_PRECISION, r->limbs);
}

static inline void mel_freq_store(Mel_Hz *r, mpfr_srcptr v)
{
  r->kind = mpfr_custom_get_kind(v);
  r->exp  = mpfr_custom_get_exp(v);
}

static inline void mel_freq_scratch(mpfr_t out, mp_limb_t *limbs)
{
  mpfr_custom_init(limbs, MEL_FREQ_PRECISION);
  mpfr_custom_init_set(out, MPFR_REGULAR_KIND, 0, MEL_FREQ_PRECISION, limbs);
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Hz mel_freq(double value)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz f;
  mpfr_t v;
  mel_freq_bind_out(v, &f);
  mpfr_set_d(v, value, MPFR_RNDN);
  mel_freq_store(&f, v);
  return f;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Hz mel_freq(mpfr_srcptr value)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz f;
  mpfr_t v;
  mel_freq_bind_out(v, &f);
  mpfr_set(v, value, MPFR_RNDN);
  mel_freq_store(&f, v);
  return f;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Hz mel_freq(mpq_srcptr value)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz f;
  mpfr_t v;
  mel_freq_bind_out(v, &f);
  mpfr_set_q(v, value, MPFR_RNDN);
  mel_freq_store(&f, v);
  return f;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Hz mel_freq(unsigned num, unsigned den)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz f;
  mpfr_t v;
  mel_freq_bind_out(v, &f);
  mpfr_set_ui(v, num, MPFR_RNDN);
  mpfr_div_ui(v, v, den, MPFR_RNDN);
  mel_freq_store(&f, v);
  return f;
}

MEL_NODISCARD double mel_freq_to_double(Mel_Hz f)
{
  MEL_FREQ_PROTECT_FLAGS;
  mpfr_t v;
  mel_freq_view(v, &f);
  return mpfr_get_d(v, MPFR_RNDN);
}

MEL_NODISCARD Mel_Hz mel_freq_add(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vb, vr;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_bind_out(vr, &r);
  mpfr_add(vr, va, vb, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_sub(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vb, vr;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_bind_out(vr, &r);
  mpfr_sub(vr, va, vb, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_neg(Mel_Hz f)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr;
  mel_freq_view(vf, &f);
  mel_freq_bind_out(vr, &r);
  mpfr_neg(vr, vf, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Hz mel_freq_mul(Mel_Hz a, mpfr_srcptr b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vr;
  mel_freq_view(va, &a);
  mel_freq_bind_out(vr, &r);
  mpfr_mul(vr, va, b, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Hz mel_freq_mul(Mel_Hz a, double b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vr;
  mel_freq_view(va, &a);
  mel_freq_bind_out(vr, &r);
  mpfr_mul_d(vr, va, b, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Hz mel_freq_div(Mel_Hz a, mpfr_srcptr b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vr;
  mel_freq_view(va, &a);
  mel_freq_bind_out(vr, &r);
  mpfr_div(vr, va, b, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Hz mel_freq_div(Mel_Hz a, double b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vr;
  mel_freq_view(va, &a);
  mel_freq_bind_out(vr, &r);
  mpfr_div_d(vr, va, b, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE double mel_freq_ratio(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  mpfr_t va, vb, r;
  mp_limb_t r_limbs[MEL_FREQ_LIMBS];
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_scratch(r, r_limbs);
  mpfr_div(r, va, vb, MPFR_RNDN);
  return mpfr_get_d(r, MPFR_RNDN);
}

MEL_OVERLOADABLE void mel_freq_ratio(mpfr_ptr out, Mel_Hz a, Mel_Hz b)
{
  mpfr_t va, vb;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mpfr_div(out, va, vb, MPFR_RNDN);
}

MEL_NODISCARD Mel_Hz mel_freq_transpose(Mel_Hz f, mpq_srcptr ratio)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr, factor;
  mp_limb_t factor_limbs[MEL_FREQ_LIMBS];
  mel_freq_view(vf, &f);
  mel_freq_bind_out(vr, &r);
  mel_freq_scratch(factor, factor_limbs);
  mpfr_set_q(factor, ratio, MPFR_RNDN);
  mpfr_mul(vr, vf, factor, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_transpose_cents(Mel_Hz f, mpfr_srcptr cents)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr, factor;
  mp_limb_t factor_limbs[MEL_FREQ_LIMBS];
  mel_freq_view(vf, &f);
  mel_freq_bind_out(vr, &r);
  mel_freq_scratch(factor, factor_limbs);
  mpfr_div_ui(factor, cents, 1200, MPFR_RNDN);
  mpfr_exp2(factor, factor, MPFR_RNDN);
  mpfr_mul(vr, vf, factor, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_transpose_semitones(Mel_Hz f, mpfr_srcptr semitones)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr, factor;
  mp_limb_t factor_limbs[MEL_FREQ_LIMBS];
  mel_freq_view(vf, &f);
  mel_freq_bind_out(vr, &r);
  mel_freq_scratch(factor, factor_limbs);
  mpfr_div_ui(factor, semitones, 12, MPFR_RNDN);
  mpfr_exp2(factor, factor, MPFR_RNDN);
  mpfr_mul(vr, vf, factor, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_octave_up(Mel_Hz f)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr;
  mel_freq_view(vf, &f);
  mel_freq_bind_out(vr, &r);
  mpfr_mul_ui(vr, vf, 2, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_octave_down(Mel_Hz f)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr;
  mel_freq_view(vf, &f);
  mel_freq_bind_out(vr, &r);
  mpfr_div_ui(vr, vf, 2, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_harmonic(Mel_Hz f, unsigned n)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr;
  mel_freq_view(vf, &f);
  mel_freq_bind_out(vr, &r);
  mpfr_mul_ui(vr, vf, n, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_midpoint(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vb, vr;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_bind_out(vr, &r);
  mpfr_add(vr, va, vb, MPFR_RNDN);
  mpfr_div_ui(vr, vr, 2, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_mod(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vb, vr, q;
  mp_limb_t q_limbs[MEL_FREQ_LIMBS];
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_bind_out(vr, &r);
  mel_freq_scratch(q, q_limbs);
  mpfr_div(q, va, vb, MPFR_RNDN);
  mpfr_trunc(q, q);
  mpfr_mul(q, q, vb, MPFR_RNDN);
  mpfr_sub(vr, va, q, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_floordiv(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vb, vr;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_bind_out(vr, &r);
  mpfr_div(vr, va, vb, MPFR_RNDN);
  mpfr_floor(vr, vr);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_abs(Mel_Hz a)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vr;
  mel_freq_view(va, &a);
  mel_freq_bind_out(vr, &r);
  mpfr_abs(vr, va, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_min(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vb, vr;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_bind_out(vr, &r);
  mpfr_min(vr, va, vb, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_max(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vb, vr;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_bind_out(vr, &r);
  mpfr_max(vr, va, vb, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}

MEL_NODISCARD uint8_t mel_freq_cmp(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  mpfr_t va, vb;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  int cmp = mpfr_cmp(va, vb);
  if (cmp < 0) return 0;
  if (cmp > 0) return 2;
  return 1;
}

MEL_NODISCARD uint8_t mel_freq_eq(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  mpfr_t va, vb;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  return mpfr_equal_p(va, vb) ? 1 : 0;
}

MEL_NODISCARD uint8_t mel_freq_near(Mel_Hz a, Mel_Hz b, mpfr_srcptr tolerance)
{
  MEL_FREQ_PROTECT_FLAGS;
  mpfr_t va, vb, diff;
  mp_limb_t diff_limbs[MEL_FREQ_LIMBS];
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_scratch(diff, diff_limbs);
  mpfr_sub(diff, va, vb, MPFR_RNDN);
  mpfr_abs(diff, diff, MPFR_RNDN);
  return mpfr_lessequal_p(diff, tolerance) ? 1 : 0;
}

MEL_NODISCARD uint8_t mel_freq_is_zero(Mel_Hz f)
{
  MEL_FREQ_PROTECT_FLAGS;
  mpfr_t vf;
  mel_freq_view(vf, &f);
  return mpfr_zero_p(vf) ? 1 : 0;
}

MEL_NODISCARD Mel_Hz mel_freq_beat(Mel_Hz a, Mel_Hz b)
{
  MEL_FREQ_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t va, vb, vr;
  mel_freq_view(va, &a);
  mel_freq_view(vb, &b);
  mel_freq_bind_out(vr, &r);
  mpfr_sub(vr, va, vb, MPFR_RNDN);
  mpfr_abs(vr, vr, MPFR_RNDN);
  mel_freq_store(&r, vr);
  return r;
}
