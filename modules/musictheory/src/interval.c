#include <musictheory/interval.h>

#include <assert.h>
#include <math/real.h>

Mel_Interval mel_interval_make(const Mel_Tuning* tuning, i64 ref_index, i64 diff)
{
    assert(tuning);
    return (Mel_Interval){ .tuning = tuning, .ref_index = ref_index, .diff = diff };
}

Mel_Interval mel_interval_from_pitches(Mel_Pitch source, Mel_Pitch target)
{
    assert(source.tuning == target.tuning);
    return mel_interval_make(source.tuning, source.index, target.index - source.index);
}

Mel_Interval mel_interval_abs(Mel_Interval i)
{
    if (i.diff >= 0)
        return i;
    return mel_interval_negate(i);
}

i64 mel_interval_abs_diff(Mel_Interval i) { return i.diff >= 0 ? i.diff : -i.diff; }

Mel_Interval mel_interval_negate(Mel_Interval i) { return mel_interval_make(i.tuning, i.ref_index + i.diff, -i.diff); }

Mel_Interval mel_interval_add(Mel_Interval a, Mel_Interval b)
{
    assert(a.tuning == b.tuning);
    return mel_interval_make(a.tuning, a.ref_index, a.diff + b.diff);
}

Mel_Interval mel_interval_mul(Mel_Interval i, i64 scalar) { return mel_interval_make(i.tuning, i.ref_index, i.diff * scalar); }

void mel_interval_ratio(mpfr_ptr out, Mel_Interval i)
{
    Mel_Hz from = mel_tuning_frequency_for_index(i.tuning, i.ref_index);
    Mel_Hz to = mel_tuning_frequency_for_index(i.tuning, i.ref_index + i.diff);
    mel_freq_ratio(out, to, from);
}

Mel_Cent mel_interval_cents(Mel_Interval i)
{
    MEL_REAL_PROTECT_FLAGS;
    mpfr_t    ratio;
    mp_limb_t ratio_limbs[MEL_REAL_LIMBS];
    mel_real_scratch(ratio, ratio_limbs);
    mel_interval_ratio(ratio, i);
    return mel_cent_from_ratio_c(ratio);
}

u8 mel_interval_eq(Mel_Interval a, Mel_Interval b)
{
    if (a.tuning == b.tuning && a.ref_index == b.ref_index)
        return a.diff == b.diff ? 1 : 0;
    return mel_interval_cmp(a, b) == 1 ? 1 : 0;
}

u8 mel_interval_cmp(Mel_Interval a, Mel_Interval b)
{
    if (a.tuning == b.tuning && a.ref_index == b.ref_index)
    {
        if (a.diff < b.diff)
            return 0;
        if (a.diff > b.diff)
            return 2;
        return 1;
    }

    MEL_REAL_PROTECT_FLAGS;
    mpfr_t    ra, rb;
    mp_limb_t ra_limbs[MEL_REAL_LIMBS];
    mp_limb_t rb_limbs[MEL_REAL_LIMBS];
    mel_real_scratch(ra, ra_limbs);
    mel_real_scratch(rb, rb_limbs);
    mel_interval_ratio(ra, a);
    mel_interval_ratio(rb, b);

    int cmp = mpfr_cmp(ra, rb);
    if (cmp < 0)
        return 0;
    if (cmp > 0)
        return 2;
    return 1;
}
