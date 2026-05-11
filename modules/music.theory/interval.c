#include "interval.h"
#include "cent.h"
#include "pitch.h"
#include <stdlib.h>

#define MEL_INTERVAL_PRECISION 256

static mpfr_prec_t mel_interval_precision(void)
{
  return MEL_INTERVAL_PRECISION;
}

void mel_interval_free(Mel_Interval* i)
{
  if (!i) return;
  mpfr_clear(i->ratio);
}

Mel_Interval mel_interval_from_pitches(Mel_Pitch source, Mel_Pitch target)
{
  Mel_Interval i;
  i.tuning = source.tuning;
  i.pitch_diff = target.pitch_index - source.pitch_index;
  i.ref_pitch_index = source.pitch_index;
  mpfr_init2(i.ratio, mel_interval_precision());
  mpfr_div(i.ratio, target.frequency.value, source.frequency.value, MPFR_RNDN);
  return i;
}

Mel_Interval mel_interval_copy(Mel_Interval i)
{
  Mel_Interval c;
  c.tuning = i.tuning;
  c.pitch_diff = i.pitch_diff;
  c.ref_pitch_index = i.ref_pitch_index;
  mpfr_init2(c.ratio, mel_interval_precision());
  mpfr_set(c.ratio, i.ratio, MPFR_RNDN);
  return c;
}

Mel_Interval mel_interval_abs(Mel_Interval i)
{
  if (i.pitch_diff >= 0) return i;

  Mel_Interval abs_i;
  abs_i.tuning = i.tuning;
  abs_i.pitch_diff = -i.pitch_diff;
  abs_i.ref_pitch_index = i.ref_pitch_index + i.pitch_diff;
  mpfr_init2(abs_i.ratio, mel_interval_precision());
  mpfr_ui_div(abs_i.ratio, 1, i.ratio, MPFR_RNDN);

  return abs_i;
}

int64_t mel_interval_abs_diff(Mel_Interval i)
{
  return i.pitch_diff >= 0 ? i.pitch_diff : -i.pitch_diff;
}

Mel_Interval mel_interval_negate(Mel_Interval i)
{
  Mel_Interval neg;
  neg.tuning = i.tuning;
  neg.pitch_diff = -i.pitch_diff;
  neg.ref_pitch_index = i.ref_pitch_index + i.pitch_diff;
  mpfr_init2(neg.ratio, mel_interval_precision());
  mpfr_ui_div(neg.ratio, 1, i.ratio, MPFR_RNDN);

  return neg;
}

Mel_Interval mel_interval_add(Mel_Interval a, Mel_Interval b)
{
  Mel_Interval sum;
  sum.tuning = a.tuning;
  sum.pitch_diff = a.pitch_diff + b.pitch_diff;
  sum.ref_pitch_index = a.ref_pitch_index;
  mpfr_init2(sum.ratio, mel_interval_precision());
  mpfr_mul(sum.ratio, a.ratio, b.ratio, MPFR_RNDN);

  return sum;
}

Mel_Interval mel_interval_mul(Mel_Interval i, int64_t scalar)
{
  Mel_Interval r;
  r.tuning = i.tuning;
  r.pitch_diff = i.pitch_diff * scalar;
  r.ref_pitch_index = i.ref_pitch_index;
  mpfr_init2(r.ratio, mel_interval_precision());
  mpfr_pow_si(r.ratio, i.ratio, scalar, MPFR_RNDN);

  return r;
}

void mel_interval_cents(mpfr_ptr out, Mel_Interval i)
{
  mel_cent_from_ratio(out, i.ratio);
}

uint8_t mel_interval_eq(Mel_Interval a, Mel_Interval b)
{
  return mpfr_equal_p(a.ratio, b.ratio) ? 1 : 0;
}

uint8_t mel_interval_cmp(Mel_Interval a, Mel_Interval b)
{
  int cmp = mpfr_cmp(a.ratio, b.ratio);
  if (cmp < 0) return 0;
  if (cmp > 0) return 2;
  return 1;
}
