#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <frequency/cent.h>

#include "pitch.h"

typedef struct Mel_PitchAdjusted Mel_PitchAdjusted;

struct Mel_PitchAdjusted
{
    Mel_Pitch base;
    Mel_Cent  adjustment;
};

MEL_NODISCARD Mel_PitchAdjusted mel_pitch_adjusted_make(Mel_Pitch base, Mel_Cent adjustment);

MEL_NODISCARD Mel_Hz mel_pitch_adjusted_frequency(Mel_PitchAdjusted pa);

MEL_NODISCARD Mel_Pitch mel_pitch_adjusted_to_pitch(Mel_PitchAdjusted pa);

MEL_NODISCARD Mel_PitchAdjusted mel_pitch_adjusted_transpose(Mel_PitchAdjusted pa, i64 diff);

MEL_NODISCARD Mel_PitchAdjusted mel_pitch_adjusted_transpose_cents(Mel_PitchAdjusted pa, Mel_Cent cents);

MEL_NODISCARD u8 mel_pitch_adjusted_eq(Mel_PitchAdjusted a, Mel_PitchAdjusted b);
MEL_NODISCARD u8 mel_pitch_adjusted_cmp(Mel_PitchAdjusted a, Mel_PitchAdjusted b);
