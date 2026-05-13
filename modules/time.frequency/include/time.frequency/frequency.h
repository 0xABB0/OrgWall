#pragma once

#include <stdint.h>
#include <mpfr.h>
#include <gmp.h>

#include <core/compiler.h>
#include <math/math.real.h>

typedef struct Mel_Hz Mel_Hz;

struct Mel_Hz
{
  Mel_Real v;
};

MEL_NODISCARD Mel_Hz mel_freq_transpose_cents(Mel_Hz f, mpfr_srcptr cents);
MEL_NODISCARD Mel_Hz mel_freq_transpose_semitones(Mel_Hz f, mpfr_srcptr semitones);

#include "frequency.inl"
