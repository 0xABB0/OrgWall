#include "tuning.h"
#include <stdlib.h>

#define MEL_TUNING_PRECISION 256

static mpfr_prec_t mel_tuning_precision(void)
{
  return MEL_TUNING_PRECISION;
}

void mel_tuning_free(Mel_Tuning* t)
{
  if (!t) return;
  switch (t->kind)
  {
  case MEL_TUNING_ED:
    mpq_clear(t->ed.eq_ratio);
    break;
  case MEL_TUNING_EDO:
    break;
  case MEL_TUNING_CUSTOM:
    mpfr_clear(t->custom.eq_ratio);
    for (uint32_t i = 0; i < t->custom.steps_count; i++)
      mpfr_clear(t->custom.steps[i]);
    free(t->custom.steps);
    break;
  }
}

Mel_Tuning mel_tuning_ed(uint32_t divisions, uint64_t eq_num, uint64_t eq_den, Mel_Hz ref_frequency)
{
  Mel_Tuning t = {0};
  t.kind = MEL_TUNING_ED;
  t.ref_frequency = ref_frequency;
  t.ed.divisions = divisions;
  mpq_init(t.ed.eq_ratio);
  mpq_set_ui(t.ed.eq_ratio, eq_num, eq_den);
  mpq_canonicalize(t.ed.eq_ratio);
  return t;
}

Mel_Tuning mel_tuning_edo(uint32_t divisions, Mel_Hz ref_frequency)
{
  Mel_Tuning t = {0};
  t.kind = MEL_TUNING_EDO;
  t.ref_frequency = ref_frequency;
  t.edo.divisions = divisions;
  return t;
}

Mel_Tuning mel_tuning_custom(uint32_t steps_count, Mel_Hz ref_frequency)
{
  Mel_Tuning t = {0};
  t.kind = MEL_TUNING_CUSTOM;
  t.ref_frequency = ref_frequency;
  t.custom.steps_count = steps_count;
  mpfr_init2(t.custom.eq_ratio, mel_tuning_precision());
  mpfr_set_ui(t.custom.eq_ratio, 2, MPFR_RNDN);
  t.custom.steps = malloc(steps_count * sizeof(mpfr_t));
  for (uint32_t i = 0; i < steps_count; i++)
    mpfr_init2(t.custom.steps[i], mel_tuning_precision());
  return t;
}

void mel_tuning_custom_set_step(Mel_Tuning* t, uint32_t idx, mpfr_srcptr ratio)
{
  mpfr_set(t->custom.steps[idx], ratio, MPFR_RNDN);
}

void mel_tuning_custom_set_step_rational(Mel_Tuning* t, uint32_t idx, uint64_t num, uint64_t den)
{
  mpfr_set_ui(t->custom.steps[idx], num, MPFR_RNDN);
  mpfr_div_ui(t->custom.steps[idx], t->custom.steps[idx], den, MPFR_RNDN);
}

void mel_tuning_custom_set_eq_ratio(Mel_Tuning* t, mpfr_srcptr ratio)
{
  mpfr_set(t->custom.eq_ratio, ratio, MPFR_RNDN);
}

Mel_Hz mel_tuning_frequency_for_index(const Mel_Tuning* t, int64_t pitch_index)
{
  mpfr_t step;
  mpfr_init2(step, mel_tuning_precision());

  switch (t->kind)
  {
  case MEL_TUNING_ED:
    mpfr_set_q(step, t->ed.eq_ratio, MPFR_RNDN);
    mpfr_rootn_ui(step, step, t->ed.divisions, MPFR_RNDN);
    mpfr_pow_si(step, step, pitch_index, MPFR_RNDN);
    break;
  case MEL_TUNING_EDO:
    mpfr_set_ui(step, 2, MPFR_RNDN);
    mpfr_rootn_ui(step, step, t->edo.divisions, MPFR_RNDN);
    mpfr_pow_si(step, step, pitch_index, MPFR_RNDN);
    break;
  case MEL_TUNING_CUSTOM:
  {
    uint32_t period = t->custom.steps_count;
    int64_t bi = pitch_index / (int64_t)period;
    uint32_t pc = (uint32_t)((pitch_index % (int64_t)period + period) % period);

    mpfr_set(step, t->custom.steps[pc], MPFR_RNDN);

    if (bi != 0)
    {
      mpfr_t period_ratio;
      mpfr_init2(period_ratio, mel_tuning_precision());
      mpfr_set(period_ratio, t->custom.eq_ratio, MPFR_RNDN);
      mpfr_pow_si(period_ratio, period_ratio, bi, MPFR_RNDN);
      mpfr_mul(step, step, period_ratio, MPFR_RNDN);
      mpfr_clear(period_ratio);
    }
    break;
  }
  }

  Mel_Hz f = mel_freq_mul(t->ref_frequency, step);
  mpfr_clear(step);
  return f;
}

int64_t mel_tuning_find_index(const Mel_Tuning* t, Mel_Hz frequency)
{
  int64_t lo;
  int64_t hi;

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
    int64_t mid = lo + (hi - lo) / 2;
    Mel_Hz mid_freq = mel_tuning_frequency_for_index(t, mid);
    uint8_t cmp = mel_freq_cmp(mid_freq, frequency);
    if (cmp == 1) return mid;
    if (cmp == 0) lo = mid;
    else hi = mid;
  }

  Mel_Hz lo_freq = mel_tuning_frequency_for_index(t, lo);
  Mel_Hz hi_freq = mel_tuning_frequency_for_index(t, hi);

  Mel_Hz diff_lo = mel_freq_abs(mel_freq_sub(frequency, lo_freq));
  Mel_Hz diff_hi = mel_freq_abs(mel_freq_sub(frequency, hi_freq));

  return mel_freq_cmp(diff_lo, diff_hi) == 0 ? lo : hi;
}

uint32_t mel_tuning_period_length(const Mel_Tuning* t)
{
  switch (t->kind)
  {
  case MEL_TUNING_ED:     return t->ed.divisions;
  case MEL_TUNING_EDO:    return t->edo.divisions;
  case MEL_TUNING_CUSTOM: return t->custom.steps_count;
  }
  return 0;
}

uint8_t mel_tuning_is_periodic(const Mel_Tuning* t)
{
  return mel_tuning_period_length(t) > 0;
}

int32_t mel_tuning_get_generators(const Mel_Tuning* t, int64_t* out_indices, int32_t max_count)
{
  uint32_t period = mel_tuning_period_length(t);
  if (period == 0) return 0;

  int32_t count = 0;
  for (int64_t idx = 1; idx <= (int64_t)period && count < max_count; idx++)
  {
    int64_t a = (int64_t)period;
    int64_t b = idx;
    while (b != 0)
    {
      int64_t t_val = b;
      b = a % b;
      a = t_val;
    }
    if (a == 1)
    {
      out_indices[count] = idx;
      count++;
    }
  }
  return count;
}
