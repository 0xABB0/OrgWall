#include <tuning/tuning.h>

#include <assert.h>

typedef struct
{
    Mel_Real step;
} Mel_Tuning_Ed_Ctx;

typedef struct
{
    u32      count;
    Mel_Real eq_ratio;
    Mel_Real steps[];
} Mel_Tuning_Custom_Ctx;

static void mel_tuning__ctx_destroy(void* ctx, const Mel_Alloc* alloc) { mel_dealloc(alloc, ctx); }

static Mel_Hz mel_tuning__ed_frequency(const void* ctx, Mel_Hz ref, i64 index)
{
    const Mel_Tuning_Ed_Ctx* c = ctx;
    MEL_REAL_PROTECT_FLAGS;
    mpfr_t    step, factor;
    mp_limb_t factor_limbs[MEL_REAL_LIMBS];
    mel_real_view(step, &c->step);
    mel_real_scratch(factor, factor_limbs);
    mpfr_pow_si(factor, step, (long)index, MPFR_RNDN);
    return mel_freq_mul(ref, factor);
}

static Mel_Hz mel_tuning__custom_frequency(const void* ctx, Mel_Hz ref, i64 index)
{
    const Mel_Tuning_Custom_Ctx* c = ctx;

    i64 period = (i64)c->count;
    i64 bi = index >= 0 ? index / period : -((-index + period - 1) / period);
    i64 pc = index - bi * period;

    MEL_REAL_PROTECT_FLAGS;
    mpfr_t    step, factor;
    mp_limb_t factor_limbs[MEL_REAL_LIMBS];
    mel_real_view(step, &c->steps[pc]);
    mel_real_scratch(factor, factor_limbs);

    if (bi == 0)
    {
        mpfr_set(factor, step, MPFR_RNDN);
    }
    else
    {
        mpfr_t eq;
        mel_real_view(eq, &c->eq_ratio);
        mpfr_pow_si(factor, eq, (long)bi, MPFR_RNDN);
        mpfr_mul(factor, factor, step, MPFR_RNDN);
    }

    return mel_freq_mul(ref, factor);
}

void mel_tuning_free(Mel_Tuning* t)
{
    if (!t || !t->ctx)
        return;
    if (t->destroy)
        t->destroy(t->ctx, t->alloc);
    t->ctx = NULL;
}

Mel_Tuning mel_tuning_ed(const Mel_Alloc* alloc, u32 divisions, u64 eq_num, u64 eq_den, Mel_Hz ref_frequency)
{
    assert(divisions > 0);
    assert(eq_num > 0 && eq_den > 0);

    Mel_Tuning_Ed_Ctx* ctx = mel_alloc_type(alloc, Mel_Tuning_Ed_Ctx);

    {
        MEL_REAL_PROTECT_FLAGS;
        mpfr_t step;
        mel_real_bind_out(step, &ctx->step);
        mpfr_set_ui(step, (unsigned long)eq_num, MPFR_RNDN);
        mpfr_div_ui(step, step, (unsigned long)eq_den, MPFR_RNDN);
        mpfr_rootn_ui(step, step, divisions, MPFR_RNDN);
        mel_real_store(&ctx->step, step);
    }

    Mel_Tuning t;
    t.ctx = ctx;
    t.alloc = alloc;
    t.ref_frequency = ref_frequency;
    t.period = divisions;
    t.frequency_for_index = mel_tuning__ed_frequency;
    t.destroy = mel_tuning__ctx_destroy;
    return t;
}

Mel_Tuning mel_tuning_edo(const Mel_Alloc* alloc, u32 divisions, Mel_Hz ref_frequency) { return mel_tuning_ed(alloc, divisions, 2, 1, ref_frequency); }

Mel_Tuning mel_tuning_custom(const Mel_Alloc* alloc, u32 steps_count, Mel_Hz ref_frequency)
{
    assert(steps_count > 0);

    Mel_Tuning_Custom_Ctx* ctx = mel_alloc(alloc, sizeof(Mel_Tuning_Custom_Ctx) + steps_count * sizeof(Mel_Real));
    ctx->count = steps_count;
    ctx->eq_ratio = mel_real(2, 1);
    for (u32 i = 0; i < steps_count; i++)
        ctx->steps[i] = mel_real(1, 1);

    Mel_Tuning t;
    t.ctx = ctx;
    t.alloc = alloc;
    t.ref_frequency = ref_frequency;
    t.period = steps_count;
    t.frequency_for_index = mel_tuning__custom_frequency;
    t.destroy = mel_tuning__ctx_destroy;
    return t;
}

void mel_tuning_custom_set_step(Mel_Tuning* t, u32 idx, mpfr_srcptr ratio)
{
    assert(t->frequency_for_index == mel_tuning__custom_frequency);
    Mel_Tuning_Custom_Ctx* ctx = t->ctx;
    assert(idx < ctx->count);
    ctx->steps[idx] = mel_real(ratio);
}

void mel_tuning_custom_set_step_rational(Mel_Tuning* t, u32 idx, u64 num, u64 den)
{
    assert(t->frequency_for_index == mel_tuning__custom_frequency);
    assert(num > 0 && den > 0);
    Mel_Tuning_Custom_Ctx* ctx = t->ctx;
    assert(idx < ctx->count);

    MEL_REAL_PROTECT_FLAGS;
    mpfr_t step;
    mel_real_bind_out(step, &ctx->steps[idx]);
    mpfr_set_ui(step, (unsigned long)num, MPFR_RNDN);
    mpfr_div_ui(step, step, (unsigned long)den, MPFR_RNDN);
    mel_real_store(&ctx->steps[idx], step);
}

void mel_tuning_custom_set_eq_ratio(Mel_Tuning* t, mpfr_srcptr ratio)
{
    assert(t->frequency_for_index == mel_tuning__custom_frequency);
    Mel_Tuning_Custom_Ctx* ctx = t->ctx;
    ctx->eq_ratio = mel_real(ratio);
}

Mel_Hz mel_tuning_frequency_for_index(const Mel_Tuning* t, i64 pitch_index)
{
    assert(t->frequency_for_index);
    return t->frequency_for_index(t->ctx, t->ref_frequency, pitch_index);
}

i64 mel_tuning_find_index(const Mel_Tuning* t, Mel_Hz frequency)
{
    assert(mel_freq_cmp(frequency, mel_freq(0.0)) == 2);

    i64 lo;
    i64 hi;

    Mel_Hz base_freq = mel_tuning_frequency_for_index(t, 0);
    if (mel_freq_cmp(frequency, base_freq) != 2)
    {
        lo = -1;
        Mel_Hz lo_f = mel_tuning_frequency_for_index(t, lo);
        while (mel_freq_cmp(frequency, lo_f) != 2)
        {
            lo *= 2;
            lo_f = mel_tuning_frequency_for_index(t, lo);
        }
        hi = 0;
    }
    else
    {
        hi = 1;
        Mel_Hz hi_f = mel_tuning_frequency_for_index(t, hi);
        while (mel_freq_cmp(frequency, hi_f) != 0)
        {
            hi *= 2;
            hi_f = mel_tuning_frequency_for_index(t, hi);
        }
        lo = 0;
    }

    while ((hi - lo) > 1)
    {
        i64     mid = lo + (hi - lo) / 2;
        Mel_Hz  mid_freq = mel_tuning_frequency_for_index(t, mid);
        uint8_t cmp = mel_freq_cmp(mid_freq, frequency);
        if (cmp == 1)
            return mid;
        if (cmp == 0)
            lo = mid;
        else
            hi = mid;
    }

    Mel_Hz lo_freq = mel_tuning_frequency_for_index(t, lo);
    Mel_Hz hi_freq = mel_tuning_frequency_for_index(t, hi);

    Mel_Hz diff_lo = mel_freq_abs(mel_freq_sub(frequency, lo_freq));
    Mel_Hz diff_hi = mel_freq_abs(mel_freq_sub(frequency, hi_freq));

    return mel_freq_cmp(diff_lo, diff_hi) == 0 ? lo : hi;
}

i32 mel_tuning_get_generators(const Mel_Tuning* t, i64* out_indices, i32 max_count)
{
    u32 period = t->period;
    if (period == 0)
        return 0;

    i32 count = 0;
    for (i64 idx = 1; idx <= (i64)period && count < max_count; idx++)
    {
        i64 a = (i64)period;
        i64 b = idx;
        while (b != 0)
        {
            i64 r = a % b;
            a = b;
            b = r;
        }
        if (a == 1)
        {
            out_indices[count] = idx;
            count++;
        }
    }
    return count;
}
