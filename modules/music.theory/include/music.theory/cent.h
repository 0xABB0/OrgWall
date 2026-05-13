#pragma once

#include <stdint.h>
#include <mpfr.h>
#include <gmp.h>

#include <core/compiler.h>
#include <math/real.h>
#include <time.frequency/frequency.h>

typedef struct Mel_Cent Mel_Cent;

struct Mel_Cent
{
  Mel_Real v;
};

static inline void mel_cent_view(mpfr_t out, const Mel_Cent *c);

MEL_OVERLOADABLE static inline Mel_Cent mel_cent(double value);
MEL_OVERLOADABLE static inline Mel_Cent mel_cent(mpfr_srcptr value);

MEL_NODISCARD static inline double mel_cent_to_double(Mel_Cent c);

void mel_cent_from_ratio(mpfr_ptr out, mpfr_srcptr ratio);
MEL_NODISCARD Mel_Cent mel_cent_from_ratio_c(mpfr_srcptr ratio);

void mel_cent_to_ratio(mpfr_ptr out, Mel_Cent c);
MEL_NODISCARD Mel_Cent mel_cent_to_ratio_c(Mel_Cent c);

void mel_cent_from_freqs(mpfr_ptr out, const Mel_Hz *a, const Mel_Hz *b);
MEL_NODISCARD Mel_Cent mel_cent_from_freqs_c(const Mel_Hz *a, const Mel_Hz *b);

MEL_NODISCARD static inline Mel_Cent mel_cent_add(Mel_Cent a, Mel_Cent b);
MEL_NODISCARD static inline Mel_Cent mel_cent_sub(Mel_Cent a, Mel_Cent b);
MEL_NODISCARD static inline Mel_Cent mel_cent_neg(Mel_Cent c);

MEL_NODISCARD MEL_OVERLOADABLE static inline Mel_Cent mel_cent_mul(Mel_Cent c, mpfr_srcptr scalar);
MEL_NODISCARD MEL_OVERLOADABLE static inline Mel_Cent mel_cent_mul(Mel_Cent c, double scalar);

MEL_NODISCARD MEL_OVERLOADABLE static inline Mel_Cent mel_cent_div(Mel_Cent c, mpfr_srcptr scalar);
MEL_NODISCARD MEL_OVERLOADABLE static inline Mel_Cent mel_cent_div(Mel_Cent c, double scalar);

MEL_NODISCARD static inline Mel_Cent mel_cent_abs(Mel_Cent c);
MEL_NODISCARD static inline Mel_Cent mel_cent_min(Mel_Cent a, Mel_Cent b);
MEL_NODISCARD static inline Mel_Cent mel_cent_max(Mel_Cent a, Mel_Cent b);

MEL_NODISCARD static inline uint8_t mel_cent_cmp(Mel_Cent a, Mel_Cent b);
MEL_NODISCARD static inline uint8_t mel_cent_eq(Mel_Cent a, Mel_Cent b);
MEL_NODISCARD static inline uint8_t mel_cent_near(Mel_Cent a, Mel_Cent b, mpfr_srcptr tolerance);
MEL_NODISCARD static inline uint8_t mel_cent_is_zero(Mel_Cent c);

#include "cent.inl"
