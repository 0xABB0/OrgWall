#include <time.frequency/frequency.h>

MEL_NODISCARD Mel_Hz mel_freq_transpose_cents(Mel_Hz f, mpfr_srcptr cents)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr, factor;
  mp_limb_t factor_limbs[MEL_REAL_LIMBS];
  mel_real_view(vf, &f.v);
  mel_real_bind_out(vr, &r.v);
  mel_real_scratch(factor, factor_limbs);
  mpfr_div_ui(factor, cents, 1200, MPFR_RNDN);
  mpfr_exp2(factor, factor, MPFR_RNDN);
  mpfr_mul(vr, vf, factor, MPFR_RNDN);
  mel_real_store(&r.v, vr);
  return r;
}

MEL_NODISCARD Mel_Hz mel_freq_transpose_semitones(Mel_Hz f, mpfr_srcptr semitones)
{
  MEL_REAL_PROTECT_FLAGS;
  Mel_Hz r;
  mpfr_t vf, vr, factor;
  mp_limb_t factor_limbs[MEL_REAL_LIMBS];
  mel_real_view(vf, &f.v);
  mel_real_bind_out(vr, &r.v);
  mel_real_scratch(factor, factor_limbs);
  mpfr_div_ui(factor, semitones, 12, MPFR_RNDN);
  mpfr_exp2(factor, factor, MPFR_RNDN);
  mpfr_mul(vr, vf, factor, MPFR_RNDN);
  mel_real_store(&r.v, vr);
  return r;
}
