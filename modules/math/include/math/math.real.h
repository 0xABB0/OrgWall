#pragma once

#include <stdint.h>
#include <mpfr.h>
#include <gmp.h>

#include <core/compiler.h>

#define MEL_REAL_PRECISION 256
#define MEL_REAL_LIMBS ((MEL_REAL_PRECISION + GMP_NUMB_BITS - 1) / GMP_NUMB_BITS)

typedef struct Mel_Real Mel_Real;

struct Mel_Real
{
  int        kind;
  mpfr_exp_t exp;
  mp_limb_t  limbs[MEL_REAL_LIMBS];
};

#define MEL_REAL_PROTECT_FLAGS \
  MEL_CLEANUP(mel_real_flags_restore) \
  mpfr_flags_t _mel_real_saved_flags = mpfr_flags_save()

MEL_NODISCARD MEL_OVERLOADABLE MEL_CONST Mel_Real mel_real(double value);
MEL_NODISCARD MEL_OVERLOADABLE MEL_PURE  Mel_Real mel_real(mpfr_srcptr value);
MEL_NODISCARD MEL_OVERLOADABLE MEL_PURE  Mel_Real mel_real(mpq_srcptr value);
MEL_NODISCARD MEL_OVERLOADABLE MEL_CONST Mel_Real mel_real(unsigned num, unsigned den);

MEL_NODISCARD MEL_CONST double mel_real_to_double(Mel_Real r);

MEL_NODISCARD MEL_CONST Mel_Real mel_real_add(Mel_Real a, Mel_Real b);
MEL_NODISCARD MEL_CONST Mel_Real mel_real_sub(Mel_Real a, Mel_Real b);
MEL_NODISCARD MEL_CONST Mel_Real mel_real_neg(Mel_Real r);

MEL_NODISCARD MEL_OVERLOADABLE MEL_PURE  Mel_Real mel_real_mul(Mel_Real a, mpfr_srcptr b);
MEL_NODISCARD MEL_OVERLOADABLE MEL_CONST Mel_Real mel_real_mul(Mel_Real a, double b);
MEL_NODISCARD MEL_CONST Mel_Real mel_real_mul_ui(Mel_Real a, unsigned b);
MEL_NODISCARD MEL_PURE  Mel_Real mel_real_mul_q(Mel_Real a, mpq_srcptr b);

MEL_NODISCARD MEL_OVERLOADABLE MEL_PURE  Mel_Real mel_real_div(Mel_Real a, mpfr_srcptr b);
MEL_NODISCARD MEL_OVERLOADABLE MEL_CONST Mel_Real mel_real_div(Mel_Real a, double b);
MEL_NODISCARD MEL_CONST Mel_Real mel_real_div_ui(Mel_Real a, unsigned b);

MEL_NODISCARD MEL_OVERLOADABLE MEL_CONST double mel_real_ratio(Mel_Real a, Mel_Real b);
MEL_OVERLOADABLE void mel_real_ratio(mpfr_ptr out, Mel_Real a, Mel_Real b);

MEL_NODISCARD MEL_CONST Mel_Real mel_real_mod(Mel_Real a, Mel_Real b);
MEL_NODISCARD MEL_CONST Mel_Real mel_real_floordiv(Mel_Real a, Mel_Real b);
MEL_NODISCARD MEL_CONST Mel_Real mel_real_midpoint(Mel_Real a, Mel_Real b);

MEL_NODISCARD MEL_CONST Mel_Real mel_real_abs(Mel_Real a);
MEL_NODISCARD MEL_CONST Mel_Real mel_real_min(Mel_Real a, Mel_Real b);
MEL_NODISCARD MEL_CONST Mel_Real mel_real_max(Mel_Real a, Mel_Real b);

MEL_NODISCARD MEL_CONST uint8_t mel_real_cmp(Mel_Real a, Mel_Real b);
MEL_NODISCARD MEL_CONST uint8_t mel_real_eq(Mel_Real a, Mel_Real b);
MEL_NODISCARD MEL_PURE  uint8_t mel_real_near(Mel_Real a, Mel_Real b, mpfr_srcptr tolerance);
MEL_NODISCARD MEL_CONST uint8_t mel_real_is_zero(Mel_Real r);

#include "math.real.inl"
