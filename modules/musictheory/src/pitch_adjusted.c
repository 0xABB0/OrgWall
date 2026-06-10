#include <musictheory/pitch_adjusted.h>

Mel_PitchAdjusted mel_pitch_adjusted_make(Mel_Pitch base, Mel_Cent adjustment) { return (Mel_PitchAdjusted){ .base = base, .adjustment = adjustment }; }

Mel_Hz mel_pitch_adjusted_frequency(Mel_PitchAdjusted pa)
{
    mpfr_t adj;
    mel_cent_view(adj, &pa.adjustment);
    return mel_freq_transpose_cents(mel_pitch_frequency(pa.base), adj);
}

Mel_Pitch mel_pitch_adjusted_to_pitch(Mel_PitchAdjusted pa) { return mel_pitch_make(pa.base.tuning, mel_tuning_find_index(pa.base.tuning, mel_pitch_adjusted_frequency(pa))); }

Mel_PitchAdjusted mel_pitch_adjusted_transpose(Mel_PitchAdjusted pa, i64 diff) { return mel_pitch_adjusted_make(mel_pitch_transpose(pa.base, diff), pa.adjustment); }

Mel_PitchAdjusted mel_pitch_adjusted_transpose_cents(Mel_PitchAdjusted pa, Mel_Cent cents) { return mel_pitch_adjusted_make(pa.base, mel_cent_add(pa.adjustment, cents)); }

u8 mel_pitch_adjusted_eq(Mel_PitchAdjusted a, Mel_PitchAdjusted b)
{
    if (!mel_pitch_eq(a.base, b.base))
        return 0;
    return mel_cent_eq(a.adjustment, b.adjustment);
}

u8 mel_pitch_adjusted_cmp(Mel_PitchAdjusted a, Mel_PitchAdjusted b) { return mel_freq_cmp(mel_pitch_adjusted_frequency(a), mel_pitch_adjusted_frequency(b)); }
