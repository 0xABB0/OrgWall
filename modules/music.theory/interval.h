#pragma once

#include <stdint.h>
#include <mpfr.h>
#include "tuning.h"

typedef struct Mel_Interval Mel_Interval;
typedef struct Mel_Pitch Mel_Pitch;

struct Mel_Interval
{
  const Mel_Tuning* tuning;
  mpfr_t ratio;
  int64_t pitch_diff;
  int64_t ref_pitch_index;
};

void mel_interval_free(Mel_Interval* i);
static inline void mel_interval_cleanup(Mel_Interval* i) { mel_interval_free(i); }
#define Mel_Interval_AUTO __attribute__((cleanup(mel_interval_cleanup))) Mel_Interval

Mel_Interval mel_interval_from_pitches(Mel_Pitch source, Mel_Pitch target);

Mel_Interval mel_interval_copy(Mel_Interval i);

Mel_Interval mel_interval_abs(Mel_Interval i);

int64_t mel_interval_abs_diff(Mel_Interval i);

Mel_Interval mel_interval_negate(Mel_Interval i);

Mel_Interval mel_interval_add(Mel_Interval a, Mel_Interval b);

Mel_Interval mel_interval_mul(Mel_Interval i, int64_t scalar);

void mel_interval_cents(mpfr_ptr out, Mel_Interval i);

uint8_t mel_interval_eq(Mel_Interval a, Mel_Interval b);
uint8_t mel_interval_cmp(Mel_Interval a, Mel_Interval b);
