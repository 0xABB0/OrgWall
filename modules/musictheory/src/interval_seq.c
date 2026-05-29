#include <musictheory/interval_seq.h>
#include <musictheory/scale.h>
#include <stdlib.h>
#include <string.h>

#define MEL_INTERVAL_SEQ_INITIAL_CAPACITY 16

void mel_interval_seq_free(Mel_IntervalSeq* s)
{
  if (!s) return;
  for (int32_t i = 0; i < s->count; i++)
    mpfr_clear(s->intervals[i].ratio);
  free(s->intervals);
  s->intervals = NULL;
  s->count = 0;
  s->capacity = 0;
}

Mel_IntervalSeq mel_interval_seq_make(const Mel_Tuning* tuning)
{
  Mel_IntervalSeq s;
  s.tuning = tuning;
  s.count = 0;
  s.capacity = MEL_INTERVAL_SEQ_INITIAL_CAPACITY;
  s.intervals = malloc(s.capacity * sizeof(Mel_Interval));
  return s;
}

Mel_IntervalSeq mel_interval_seq_from_diffs(const Mel_Tuning* tuning, const int64_t* diffs, int32_t count)
{
  Mel_IntervalSeq s = mel_interval_seq_make(tuning);
  for (int32_t i = 0; i < count; i++)
  {
    Mel_Pitch ref = mel_pitch_make(tuning, 0);
    Mel_Pitch target = mel_pitch_transpose(ref, diffs[i]);
    Mel_Interval iv = mel_interval_from_pitches(ref, target);
    mel_interval_seq_add(&s, iv);
  }
  return s;
}

Mel_IntervalSeq mel_interval_seq_from_ratios(const Mel_Tuning* tuning, const uint64_t* nums, const uint64_t* dens, int32_t count)
{
  Mel_IntervalSeq s = mel_interval_seq_make(tuning);

  Mel_Pitch root = mel_pitch_make(tuning, 0);
  int64_t prev_idx = 0;

  for (int32_t i = 0; i < count; i++)
  {
    mpfr_t ratio;
    mpfr_init2(ratio, 256);
    mpfr_set_ui(ratio, nums[i], MPFR_RNDN);
    mpfr_div_ui(ratio, ratio, dens[i], MPFR_RNDN);

    Mel_Hz target_freq = mel_freq_mul(root.frequency, ratio);

    int64_t target_idx = mel_tuning_find_index(tuning, target_freq);
    Mel_Interval iv = mel_interval_from_pitches(mel_pitch_make(tuning, prev_idx), mel_pitch_make(tuning, target_idx));
    mel_interval_seq_add(&s, iv);
    prev_idx = target_idx;

    mpfr_clear(ratio);
  }

  return s;
}

Mel_IntervalSeq mel_interval_seq_from_cents(const Mel_Tuning* tuning, const double* cents, int32_t count)
{
  Mel_IntervalSeq s = mel_interval_seq_make(tuning);

  Mel_Pitch root = mel_pitch_make(tuning, 0);
  int64_t prev_idx = 0;

  for (int32_t i = 0; i < count; i++)
  {
    mpfr_t ratio;
    mpfr_init2(ratio, 256);
    mpfr_set_d(ratio, cents[i], MPFR_RNDN);
    mpfr_div_ui(ratio, ratio, 1200, MPFR_RNDN);
    mpfr_exp2(ratio, ratio, MPFR_RNDN);

    Mel_Hz target_freq = mel_freq_mul(root.frequency, ratio);

    int64_t target_idx = mel_tuning_find_index(tuning, target_freq);
    Mel_Interval iv = mel_interval_from_pitches(mel_pitch_make(tuning, prev_idx), mel_pitch_make(tuning, target_idx));
    mel_interval_seq_add(&s, iv);
    prev_idx = target_idx;

    mpfr_clear(ratio);
  }

  return s;
}

Mel_IntervalSeq mel_interval_seq_copy(const Mel_IntervalSeq* s)
{
  Mel_IntervalSeq c;
  c.tuning = s->tuning;
  c.count = s->count;
  c.capacity = s->capacity;
  c.intervals = malloc(c.capacity * sizeof(Mel_Interval));
  for (int32_t i = 0; i < s->count; i++)
    c.intervals[i] = mel_interval_copy(s->intervals[i]);
  return c;
}

static void mel_interval_seq_grow(Mel_IntervalSeq* s)
{
  s->capacity *= 2;
  s->intervals = realloc(s->intervals, s->capacity * sizeof(Mel_Interval));
}

void mel_interval_seq_add(Mel_IntervalSeq* s, Mel_Interval interval)
{
  if (s->count >= s->capacity) mel_interval_seq_grow(s);
  s->intervals[s->count] = interval;
  s->count++;
}

Mel_Interval mel_interval_seq_get(const Mel_IntervalSeq* s, int32_t idx)
{
  return mel_interval_copy(s->intervals[idx]);
}

Mel_IntervalSeq mel_interval_seq_concat(const Mel_IntervalSeq* a, const Mel_IntervalSeq* b)
{
  Mel_IntervalSeq r = mel_interval_seq_make(a->tuning);
  for (int32_t i = 0; i < a->count; i++)
    mel_interval_seq_add(&r, mel_interval_copy(a->intervals[i]));
  for (int32_t i = 0; i < b->count; i++)
    mel_interval_seq_add(&r, mel_interval_copy(b->intervals[i]));
  return r;
}

Mel_IntervalSeq mel_interval_seq_mul(const Mel_IntervalSeq* s, int32_t scalar)
{
  Mel_IntervalSeq r = mel_interval_seq_make(s->tuning);
  for (int32_t k = 0; k < scalar; k++)
    for (int32_t i = 0; i < s->count; i++)
      mel_interval_seq_add(&r, mel_interval_copy(s->intervals[i]));
  return r;
}

Mel_IntervalSeq mel_interval_seq_invert(const Mel_IntervalSeq* s)
{
  Mel_IntervalSeq r = mel_interval_seq_make(s->tuning);
  for (int32_t i = s->count - 1; i >= 0; i--)
    mel_interval_seq_add(&r, mel_interval_copy(s->intervals[i]));
  return r;
}

Mel_Scale mel_interval_seq_to_scale(const Mel_IntervalSeq* s, Mel_Pitch start)
{
  Mel_Scale scale = mel_scale_make(s->tuning);
  mel_scale_add_pitch(&scale, start);

  Mel_Pitch current = start;
  for (int32_t i = 0; i < s->count; i++)
  {
    current.pitch_index += s->intervals[i].pitch_diff;
    current.frequency = mel_tuning_frequency_for_index(s->tuning, current.pitch_index);
    mel_scale_add_pitch(&scale, current);
  }

  return scale;
}

Mel_IntervalSeq mel_scale_to_interval_seq(const Mel_Scale* s)
{
  Mel_IntervalSeq seq = mel_interval_seq_make(s->tuning);
  for (int32_t i = 0; i < s->count - 1; i++)
    mel_interval_seq_add(&seq, mel_scale_spec_interval(s, i, i + 1));
  return seq;
}
