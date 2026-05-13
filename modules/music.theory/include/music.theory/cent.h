#pragma once

#include <stdint.h>
#include <mpfr.h>
#include <gmp.h>

#include <core/compiler.h>
#include <math/math.real.h>
#include <time.frequency/frequency.h>

typedef struct Mel_Cent Mel_Cent;

struct Mel_Cent
{
  Mel_Real v;
};

void mel_cent_from_ratio(mpfr_ptr out, mpfr_srcptr ratio);
MEL_NODISCARD Mel_Cent mel_cent_from_ratio_c(mpfr_srcptr ratio);

void mel_cent_to_ratio(mpfr_ptr out, Mel_Cent c);
MEL_NODISCARD Mel_Cent mel_cent_to_ratio_c(Mel_Cent c);

void mel_cent_from_freqs(mpfr_ptr out, const Mel_Hz *a, const Mel_Hz *b);
MEL_NODISCARD Mel_Cent mel_cent_from_freqs_c(const Mel_Hz *a, const Mel_Hz *b);

#include "cent.inl"
