#include <musictheory/pattern.h>

#include <assert.h>
#include <math/real.h>

void mel_pattern_free(Mel_Pattern* p)
{
    if (!p)
        return;
    mel_array_free(&p->diffs);
}

Mel_Pattern mel_pattern_make(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    assert(alloc && tuning);
    Mel_Pattern p;
    p.tuning = tuning;
    mel_array_init(&p.diffs, alloc);
    return p;
}

Mel_Pattern mel_pattern_from_diffs(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const i64* diffs, i32 count)
{
    Mel_Pattern p = mel_pattern_make(alloc, tuning);
    mel_array_reserve(&p.diffs, (usize)count);
    for (i32 i = 0; i < count; i++)
        mel_array_push(&p.diffs, diffs[i]);
    return p;
}

Mel_Pattern mel_pattern_from_ratios(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const u64* nums, const u64* dens, i32 count)
{
    Mel_Pattern p = mel_pattern_make(alloc, tuning);

    i64 prev_idx = 0;
    for (i32 i = 0; i < count; i++)
    {
        assert(nums[i] > 0 && dens[i] > 0);

        MEL_REAL_PROTECT_FLAGS;
        mpfr_t    ratio;
        mp_limb_t ratio_limbs[MEL_REAL_LIMBS];
        mel_real_scratch(ratio, ratio_limbs);
        mpfr_set_ui(ratio, (unsigned long)nums[i], MPFR_RNDN);
        mpfr_div_ui(ratio, ratio, (unsigned long)dens[i], MPFR_RNDN);

        Mel_Hz target = mel_freq_mul(mel_tuning_frequency_for_index(tuning, prev_idx), ratio);
        i64    target_idx = mel_tuning_find_index(tuning, target);
        mel_array_push(&p.diffs, target_idx - prev_idx);
        prev_idx = target_idx;
    }
    return p;
}

Mel_Pattern mel_pattern_from_cents(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const f64* cents, i32 count)
{
    Mel_Pattern p = mel_pattern_make(alloc, tuning);

    i64 prev_idx = 0;
    for (i32 i = 0; i < count; i++)
    {
        MEL_REAL_PROTECT_FLAGS;
        mpfr_t    ratio;
        mp_limb_t ratio_limbs[MEL_REAL_LIMBS];
        mel_real_scratch(ratio, ratio_limbs);
        mpfr_set_d(ratio, cents[i], MPFR_RNDN);
        mpfr_div_ui(ratio, ratio, 1200, MPFR_RNDN);
        mpfr_exp2(ratio, ratio, MPFR_RNDN);

        Mel_Hz target = mel_freq_mul(mel_tuning_frequency_for_index(tuning, prev_idx), ratio);
        i64    target_idx = mel_tuning_find_index(tuning, target);
        mel_array_push(&p.diffs, target_idx - prev_idx);
        prev_idx = target_idx;
    }
    return p;
}

Mel_Pattern mel_pattern_from_scale(const Mel_Alloc* alloc, const Mel_Scale* s)
{
    Mel_Pattern p = mel_pattern_make(alloc, s->tuning);
    for (usize i = 0; i + 1 < s->indices.count; i++)
        mel_array_push(&p.diffs, s->indices.items[i + 1] - s->indices.items[i]);
    return p;
}

Mel_Pattern mel_pattern_copy(const Mel_Alloc* alloc, const Mel_Pattern* p)
{
    Mel_Pattern c = mel_pattern_make(alloc, p->tuning);
    mel_array_reserve(&c.diffs, p->diffs.count);
    for (usize i = 0; i < p->diffs.count; i++)
        mel_array_push(&c.diffs, p->diffs.items[i]);
    return c;
}

void mel_pattern_add(Mel_Pattern* p, i64 diff) { mel_array_push(&p->diffs, diff); }

i64 mel_pattern_get(const Mel_Pattern* p, i32 idx)
{
    assert(idx >= 0 && (usize)idx < p->diffs.count);
    return p->diffs.items[idx];
}

i64 mel_pattern_span(const Mel_Pattern* p)
{
    i64 span = 0;
    for (usize i = 0; i < p->diffs.count; i++)
        span += p->diffs.items[i];
    return span;
}

Mel_Pattern mel_pattern_concat(const Mel_Alloc* alloc, const Mel_Pattern* a, const Mel_Pattern* b)
{
    assert(a->tuning == b->tuning);
    Mel_Pattern r = mel_pattern_copy(alloc, a);
    for (usize i = 0; i < b->diffs.count; i++)
        mel_array_push(&r.diffs, b->diffs.items[i]);
    return r;
}

Mel_Pattern mel_pattern_repeat(const Mel_Alloc* alloc, const Mel_Pattern* p, i32 times)
{
    assert(times >= 0);
    Mel_Pattern r = mel_pattern_make(alloc, p->tuning);
    mel_array_reserve(&r.diffs, p->diffs.count * (usize)times);
    for (i32 k = 0; k < times; k++)
        for (usize i = 0; i < p->diffs.count; i++)
            mel_array_push(&r.diffs, p->diffs.items[i]);
    return r;
}

Mel_Pattern mel_pattern_retrograde(const Mel_Alloc* alloc, const Mel_Pattern* p)
{
    Mel_Pattern r = mel_pattern_make(alloc, p->tuning);
    mel_array_reserve(&r.diffs, p->diffs.count);
    for (usize i = p->diffs.count; i > 0; i--)
        mel_array_push(&r.diffs, p->diffs.items[i - 1]);
    return r;
}

Mel_Scale mel_pattern_to_scale(const Mel_Alloc* alloc, const Mel_Pattern* p, Mel_Pitch root)
{
    assert(root.tuning == p->tuning);
    Mel_Scale s = mel_scale_make(alloc, p->tuning);
    mel_scale_add_index(&s, root.index);
    i64 idx = root.index;
    for (usize i = 0; i < p->diffs.count; i++)
    {
        idx += p->diffs.items[i];
        mel_scale_add_index(&s, idx);
    }
    return s;
}
