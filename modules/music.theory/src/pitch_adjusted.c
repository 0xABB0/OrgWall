#include "pitch_adjusted.h"
#include "../time.frequency/frequency.h"
#include <stdlib.h>

Mel_PitchAdjusted mel_pitch_adjusted_make(Mel_Pitch base, Mel_Cent adjustment)
{
  Mel_PitchAdjusted pa;
  pa.base = base;
  pa.adjustment = adjustment;
  return pa;
}

Mel_Hz mel_pitch_adjusted_frequency(Mel_PitchAdjusted pa)
{
  mpfr_t adj;
  mel_cent_view(adj, &pa.adjustment);
  return mel_freq_transpose_cents(pa.base.frequency, adj);
}

Mel_Pitch mel_pitch_adjusted_to_pitch(Mel_PitchAdjusted pa)
{
  Mel_Hz adjusted_freq = mel_pitch_adjusted_frequency(pa);
  Mel_Pitch p;
  p.tuning = pa.base.tuning;
  p.pitch_index = mel_tuning_find_index(pa.base.tuning, adjusted_freq);
  p.frequency = mel_tuning_frequency_for_index(pa.base.tuning, p.pitch_index);
  return p;
}

Mel_PitchAdjusted mel_pitch_adjusted_copy(Mel_PitchAdjusted pa)
{
  return pa;
}

Mel_PitchAdjusted mel_pitch_adjusted_transpose(Mel_PitchAdjusted pa, int64_t diff)
{
  Mel_PitchAdjusted r;
  r.base = mel_pitch_transpose(pa.base, diff);
  r.adjustment = pa.adjustment;
  return r;
}

Mel_PitchAdjusted mel_pitch_adjusted_transpose_cents(Mel_PitchAdjusted pa, Mel_Cent cents)
{
  Mel_PitchAdjusted r;
  r.base = pa.base;
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
  return mel_freq_cmp(fa, fb);
}
