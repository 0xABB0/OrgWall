#pragma once

#include "pitch.h"
#include "cent.h"

typedef struct Mel_PitchAdjusted Mel_PitchAdjusted;

struct Mel_PitchAdjusted
{
  Mel_Pitch base;
  Mel_Cent adjustment;
};

Mel_PitchAdjusted mel_pitch_adjusted_make(Mel_Pitch base, Mel_Cent adjustment);

Mel_Hz mel_pitch_adjusted_frequency(Mel_PitchAdjusted pa);

Mel_Pitch mel_pitch_adjusted_to_pitch(Mel_PitchAdjusted pa);

Mel_PitchAdjusted mel_pitch_adjusted_copy(Mel_PitchAdjusted pa);

Mel_PitchAdjusted mel_pitch_adjusted_transpose(Mel_PitchAdjusted pa, int64_t diff);

Mel_PitchAdjusted mel_pitch_adjusted_transpose_cents(Mel_PitchAdjusted pa, Mel_Cent cents);

uint8_t mel_pitch_adjusted_eq(Mel_PitchAdjusted a, Mel_PitchAdjusted b);
uint8_t mel_pitch_adjusted_cmp(Mel_PitchAdjusted a, Mel_PitchAdjusted b);
