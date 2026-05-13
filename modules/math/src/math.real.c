#include <math/math.real.h>

MEL_NODISCARD MEL_OVERLOADABLE Mel_Real mel_real(double value)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t v;
  mel_real_bind_out(v, &r);
  mpfr_set_d(v, value, MPFR_RNDN);
  mel_real_store(&r, v);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Real mel_real(mpfr_srcptr value)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t v;
  mel_real_bind_out(v, &r);
  mpfr_set(v, value, MPFR_RNDN);
  mel_real_store(&r, v);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Real mel_real(mpq_srcptr value)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t v;
  mel_real_bind_out(v, &r);
  mpfr_set_q(v, value, MPFR_RNDN);
  mel_real_store(&r, v);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Real mel_real(unsigned num, unsigned den)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t v;
  mel_real_bind_out(v, &r);
  mpfr_set_ui(v, num, MPFR_RNDN);
  mpfr_div_ui(v, v, den, MPFR_RNDN);
  mel_real_store(&r, v);
  return r;
}

MEL_NODISCARD double mel_real_to_double(Mel_Real r)
{
  MEL_REAL_PROTECT_FLAGS;
  mpfr_t v;
  mel_real_view(v, &r);
  return mpfr_get_d(v, MPFR_RNDN);
}

MEL_NODISCARD Mel_Real mel_real_add(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vb, vr;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_bind_out(vr, &r);
  mpfr_add(vr, va, vb, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_sub(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vb, vr;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_bind_out(vr, &r);
  mpfr_sub(vr, va, vb, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_neg(Mel_Real r)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real out;
  mpfr_t vr, vout;
  mel_real_view(vr, &r);
  mel_real_bind_out(vout, &out);
  mpfr_neg(vout, vr, MPFR_RNDN);
  mel_real_store(&out, vout);
  return out;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Real mel_real_mul(Mel_Real a, mpfr_srcptr b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vr;
  mel_real_view(va, &a);
  mel_real_bind_out(vr, &r);
  mpfr_mul(vr, va, b, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Real mel_real_mul(Mel_Real a, double b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vr;
  mel_real_view(va, &a);
  mel_real_bind_out(vr, &r);
  mpfr_mul_d(vr, va, b, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_mul_ui(Mel_Real a, unsigned b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vr;
  mel_real_view(va, &a);
  mel_real_bind_out(vr, &r);
  mpfr_mul_ui(vr, va, b, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_mul_q(Mel_Real a, mpq_srcptr b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vr, factor;
  mp_limb_t factor_limbs[MEL_REAL_LIMBS];
  mel_real_view(va, &a);
  mel_real_bind_out(vr, &r);
  mel_real_scratch(factor, factor_limbs);
  mpfr_set_q(factor, b, MPFR_RNDN);
  mpfr_mul(vr, va, factor, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Real mel_real_div(Mel_Real a, mpfr_srcptr b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vr;
  mel_real_view(va, &a);
  mel_real_bind_out(vr, &r);
  mpfr_div(vr, va, b, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE Mel_Real mel_real_div(Mel_Real a, double b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vr;
  mel_real_view(va, &a);
  mel_real_bind_out(vr, &r);
  mpfr_div_d(vr, va, b, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_div_ui(Mel_Real a, unsigned b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vr;
  mel_real_view(va, &a);
  mel_real_bind_out(vr, &r);
  mpfr_div_ui(vr, va, b, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD MEL_OVERLOADABLE double mel_real_ratio(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  mpfr_t va, vb, r;
  mp_limb_t r_limbs[MEL_REAL_LIMBS];
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_scratch(r, r_limbs);
  mpfr_div(r, va, vb, MPFR_RNDN);
  return mpfr_get_d(r, MPFR_RNDN);
}

MEL_OVERLOADABLE void mel_real_ratio(mpfr_ptr out, Mel_Real a, Mel_Real b)
{
  mpfr_t va, vb;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mpfr_div(out, va, vb, MPFR_RNDN);
}

MEL_NODISCARD Mel_Real mel_real_mod(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vb, vr, q;
  mp_limb_t q_limbs[MEL_REAL_LIMBS];
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_bind_out(vr, &r);
  mel_real_scratch(q, q_limbs);
  mpfr_div(q, va, vb, MPFR_RNDN);
  mpfr_trunc(q, q);
  mpfr_mul(q, q, vb, MPFR_RNDN);
  mpfr_sub(vr, va, q, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_floordiv(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vb, vr;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_bind_out(vr, &r);
  mpfr_div(vr, va, vb, MPFR_RNDN);
  mpfr_floor(vr, vr);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_midpoint(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vb, vr;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_bind_out(vr, &r);
  mpfr_add(vr, va, vb, MPFR_RNDN);
  mpfr_div_ui(vr, vr, 2, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_abs(Mel_Real a)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vr;
  mel_real_view(va, &a);
  mel_real_bind_out(vr, &r);
  mpfr_abs(vr, va, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_min(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vb, vr;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_bind_out(vr, &r);
  mpfr_min(vr, va, vb, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD Mel_Real mel_real_max(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Real r;
  mpfr_t va, vb, vr;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_bind_out(vr, &r);
  mpfr_max(vr, va, vb, MPFR_RNDN);
  mel_real_store(&r, vr);
  return r;
}

MEL_NODISCARD uint8_t mel_real_cmp(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  mpfr_t va, vb;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  int cmp = mpfr_cmp(va, vb);
  if (cmp < 0) return 0;
  if (cmp > 0) return 2;
  return 1;
}

MEL_NODISCARD uint8_t mel_real_eq(Mel_Real a, Mel_Real b)
{
  MEL_REAL_PROTECT_FLAGS;
  mpfr_t va, vb;
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  return mpfr_equal_p(va, vb) ? 1 : 0;
}

MEL_NODISCARD uint8_t mel_real_near(Mel_Real a, Mel_Real b, mpfr_srcptr tolerance)
{
  MEL_REAL_PROTECT_FLAGS;
  mpfr_t va, vb, diff;
  mp_limb_t diff_limbs[MEL_REAL_LIMBS];
  mel_real_view(va, &a);
  mel_real_view(vb, &b);
  mel_real_scratch(diff, diff_limbs);
  mpfr_sub(diff, va, vb, MPFR_RNDN);
  mpfr_abs(diff, diff, MPFR_RNDN);
  return mpfr_lessequal_p(diff, tolerance) ? 1 : 0;
}

MEL_NODISCARD uint8_t mel_real_is_zero(Mel_Real r)
{
  MEL_REAL_PROTECT_FLAGS;
  mpfr_t vr;
  mel_real_view(vr, &r);
  return mpfr_zero_p(vr) ? 1 : 0;
}
