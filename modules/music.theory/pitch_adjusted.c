#include "pitch_adjusted.h"
#include "../time.frequency/frequency.h"
#include <stdlib.h>

void mel_pitch_adjusted_free(Mel_PitchAdjusted* pa)
{
  if (!pa) return;
  mpfr_clear(pa->base.frequency.value);
  mpfr_clear(pa->adjustment.value);
}

Mel_PitchAdjusted mel_pitch_adjusted_make(Mel_Pitch base, Mel_Cent adjustment)
{
  Mel_PitchAdjusted pa;
  pa.base = mel_pitch_copy(base);
  mpfr_init2(pa.adjustment.value, mpfr_get_prec(adjustment.value));
  mpfr_set(pa.adjustment.value, adjustment.value, MPFR_RNDN);
  return pa;
}

Mel_Hz mel_pitch_adjusted_frequency(Mel_PitchAdjusted pa)
{
  return mel_freq_transpose_cents(pa.base.frequency, pa.adjustment.value);
}

Mel_Pitch mel_pitch_adjusted_to_pitch(Mel_PitchAdjusted pa)
{
  Mel_Hz adjusted_freq = mel_pitch_adjusted_frequency(pa);
  Mel_Pitch p = mel_pitch_make(pa.base.tuning, 0);
  mpfr_clear(p.frequency.value);
  p.pitch_index = mel_tuning_find_index(pa.base.tuning, adjusted_freq);
  p.frequency = mel_tuning_frequency_for_index(pa.base.tuning, p.pitch_index);
  mpfr_clear(adjusted_freq.value);
  return p;
}

Mel_PitchAdjusted mel_pitch_adjusted_copy(Mel_PitchAdjusted pa)
{
  Mel_PitchAdjusted c;
  c.base = mel_pitch_copy(pa.base);
  mpfr_init2(c.adjustment.value, mpfr_get_prec(pa.adjustment.value));
  mpfr_set(c.adjustment.value, pa.adjustment.value, MPFR_RNDN);
  return c;
}

Mel_PitchAdjusted mel_pitch_adjusted_transpose(Mel_PitchAdjusted pa, int64_t diff)
{
  Mel_PitchAdjusted r;
  r.base = mel_pitch_transpose(pa.base, diff);
  mpfr_init2(r.adjustment.value, mpfr_get_prec(pa.adjustment.value));
  mpfr_set(r.adjustment.value, pa.adjustment.value, MPFR_RNDN);
  return r;
}

Mel_PitchAdjusted mel_pitch_adjusted_transpose_cents(Mel_PitchAdjusted pa, Mel_Cent cents)
{
  Mel_PitchAdjusted r;
  r.base = mel_pitch_copy(pa.base);
  r.adjustment = mel_cent_add(pa.adjustment, cents);
  return r;
}

uint8_t mel_pitch_adjusted_eq(Mel_PitchAdjusted a, Mel_PitchAdjusted b)
{
  if (!mel_pitch_eq(a.base, b.base)) return 0;
  return mel_cent_eq(a.adjustment, b.adjustment);
}

uint8_t mel_pitch_adjusted_cmp(Mel_PitchAdjusted a, Mel_PitchAdjusted b)
{
  Mel_Hz fa = mel_pitch_adjusted_frequency(a);
  Mel_Hz fb = mel_pitch_adjusted_frequency(b);
  int cmp = mpfr_cmp(fa.value, fb.value);
  mpfr_clear(fa.value);
  mpfr_clear(fb.value);
  if (cmp < 0) return 0;
  if (cmp > 0) return 2;
  return 1;
}
