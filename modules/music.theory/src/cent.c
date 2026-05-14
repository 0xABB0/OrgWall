#include <music.theory/cent.h>

void mel_cent_from_ratio(mpfr_ptr out, mpfr_srcptr ratio)
{
  mpfr_log2(out, ratio, MPFR_RNDN);
  mpfr_mul_ui(out, out, 1200, MPFR_RNDN);
}

MEL_NODISCARD Mel_Cent mel_cent_from_ratio_c(mpfr_srcptr ratio)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Cent c;
  mpfr_t v;
  mel_real_bind_out(v, &c.v);
  mel_cent_from_ratio(v, ratio);
  mel_real_store(&c.v, v);
  return c;
}

void mel_cent_to_ratio(mpfr_ptr out, Mel_Cent c)
{
  mpfr_t vc;
  mel_real_view(vc, &c.v);
  mpfr_div_ui(out, vc, 1200, MPFR_RNDN);
  mpfr_exp2(out, out, MPFR_RNDN);
}

MEL_NODISCARD Mel_Cent mel_cent_to_ratio_c(Mel_Cent c)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Cent r;
  mpfr_t vr;
  mel_real_bind_out(vr, &r.v);
  mel_cent_to_ratio(vr, c);
  mel_real_store(&r.v, vr);
  return r;
}

void mel_cent_from_freqs(mpfr_ptr out, const Mel_Hz *a, const Mel_Hz *b)
{
  mpfr_t va, vb, ratio;
  mp_limb_t ratio_limbs[MEL_REAL_LIMBS];
  mel_freq_view(va, a);
  mel_freq_view(vb, b);
  mel_real_scratch(ratio, ratio_limbs);
  mpfr_div(ratio, va, vb, MPFR_RNDN);
  mel_cent_from_ratio(out, ratio);
}

MEL_NODISCARD Mel_Cent mel_cent_from_freqs_c(const Mel_Hz *a, const Mel_Hz *b)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Cent c;
  mpfr_t v;
  mel_real_bind_out(v, &c.v);
  mel_cent_from_freqs(v, a, b);
  mel_real_store(&c.v, v);
  return c;
}
