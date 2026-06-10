#include <musictheory/pitch.h>

#include <assert.h>

static i64 mel_pitch__floor_div(i64 a, i64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

Mel_Pitch mel_pitch_make(const Mel_Tuning* tuning, i64 index)
{
    assert(tuning);
    return (Mel_Pitch){ .tuning = tuning, .index = index };
}

Mel_Hz mel_pitch_frequency(Mel_Pitch p) { return mel_tuning_frequency_for_index(p.tuning, p.index); }

Mel_Pitch mel_pitch_transpose(Mel_Pitch p, i64 diff) { return mel_pitch_make(p.tuning, p.index + diff); }

Mel_Pitch mel_pitch_retune(Mel_Pitch p, const Mel_Tuning* target_tuning)
{
    assert(target_tuning);
    return mel_pitch_make(target_tuning, mel_tuning_find_index(target_tuning, mel_pitch_frequency(p)));
}

i64 mel_pitch_pc_index(Mel_Pitch p)
{
    u32 period = p.tuning->period;
    if (period == 0)
        return 0;
    return p.index - mel_pitch__floor_div(p.index, (i64)period) * (i64)period;
}

i64 mel_pitch_bi_index(Mel_Pitch p)
{
    u32 period = p.tuning->period;
    if (period == 0)
        return 0;
    return mel_pitch__floor_div(p.index, (i64)period);
}

Mel_Pitch mel_pitch_transpose_bi(Mel_Pitch p, i64 bi_diff)
{
    assert(mel_tuning_is_periodic(p.tuning));
    return mel_pitch_make(p.tuning, p.index + bi_diff * (i64)p.tuning->period);
}

Mel_Pitch mel_pitch_pcs_normalized(Mel_Pitch p) { return mel_pitch_make(p.tuning, mel_pitch_pc_index(p)); }

u8 mel_pitch_is_equivalent(Mel_Pitch a, Mel_Pitch b)
{
    if (a.tuning == b.tuning)
        return mel_pitch_pc_index(a) == mel_pitch_pc_index(b) ? 1 : 0;
    return mel_pitch_eq(mel_pitch_pcs_normalized(a), mel_pitch_pcs_normalized(b));
}

i64 mel_pitch_generator_distance(Mel_Pitch pitch, Mel_Pitch generator)
{
    assert(pitch.tuning == generator.tuning);
    assert(mel_tuning_is_periodic(pitch.tuning));

    i64 period = (i64)pitch.tuning->period;
    i64 gen_pc = mel_pitch_pc_index(generator);
    if (gen_pc == 0)
        return -1;

    i64 pc = 0;
    i64 dist = 0;
    i64 target_pc = mel_pitch_pc_index(pitch);

    while (pc != target_pc)
    {
        dist++;
        pc = (pc + gen_pc) % period;
        if (dist > period)
            return -1;
    }

    i64 alt_dist = period - dist;
    return dist < alt_dist ? dist : alt_dist;
}

u8 mel_pitch_eq(Mel_Pitch a, Mel_Pitch b)
{
    if (a.tuning == b.tuning)
        return a.index == b.index ? 1 : 0;
    return mel_freq_eq(mel_pitch_frequency(a), mel_pitch_frequency(b));
}

u8 mel_pitch_cmp(Mel_Pitch a, Mel_Pitch b)
{
    if (a.tuning == b.tuning)
    {
        if (a.index < b.index)
            return 0;
        if (a.index > b.index)
            return 2;
        return 1;
    }
    return mel_freq_cmp(mel_pitch_frequency(a), mel_pitch_frequency(b));
}
