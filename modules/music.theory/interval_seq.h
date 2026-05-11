#pragma once

#include <stdint.h>
#include "tuning.h"
#include "interval.h"

typedef struct Mel_Scale Mel_Scale;
typedef struct Mel_Pitch Mel_Pitch;

typedef struct Mel_IntervalSeq Mel_IntervalSeq;

struct Mel_IntervalSeq
{
  const Mel_Tuning* tuning;
  Mel_Interval* intervals;
  int32_t count;
  int32_t capacity;
};

void mel_interval_seq_free(Mel_IntervalSeq* s);
static inline void mel_interval_seq_cleanup(Mel_IntervalSeq* s) { mel_interval_seq_free(s); }
#define Mel_IntervalSeq_AUTO __attribute__((cleanup(mel_interval_seq_cleanup))) Mel_IntervalSeq

Mel_IntervalSeq mel_interval_seq_make(const Mel_Tuning* tuning);

Mel_IntervalSeq mel_interval_seq_from_diffs(const Mel_Tuning* tuning, const int64_t* diffs, int32_t count);

Mel_IntervalSeq mel_interval_seq_from_ratios(const Mel_Tuning* tuning, const uint64_t* nums, const uint64_t* dens, int32_t count);

Mel_IntervalSeq mel_interval_seq_from_cents(const Mel_Tuning* tuning, const double* cents, int32_t count);

Mel_IntervalSeq mel_interval_seq_copy(const Mel_IntervalSeq* s);

void mel_interval_seq_add(Mel_IntervalSeq* s, Mel_Interval interval);

Mel_Interval mel_interval_seq_get(const Mel_IntervalSeq* s, int32_t idx);

Mel_IntervalSeq mel_interval_seq_concat(const Mel_IntervalSeq* a, const Mel_IntervalSeq* b);

Mel_IntervalSeq mel_interval_seq_mul(const Mel_IntervalSeq* s, int32_t scalar);

Mel_IntervalSeq mel_interval_seq_invert(const Mel_IntervalSeq* s);

Mel_Scale mel_interval_seq_to_scale(const Mel_IntervalSeq* s, Mel_Pitch start);
