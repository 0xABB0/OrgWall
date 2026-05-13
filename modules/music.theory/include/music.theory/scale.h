#pragma once

#include <core/compiler.h>

#include <stdint.h>
#include "tuning.h"
#include "pitch.h"
#include "interval.h"

typedef struct Mel_Scale Mel_Scale;

struct Mel_Scale
{
  const Mel_Tuning* tuning;
  Mel_Pitch* pitches;
  int32_t count;
  int32_t capacity;
};

void mel_scale_free(Mel_Scale* s);
static inline void mel_scale_cleanup(Mel_Scale* s) { mel_scale_free(s); }
#define Mel_Scale_AUTO MEL_CLEANUP(mel_scale_cleanup) Mel_Scale

Mel_Scale mel_scale_make(const Mel_Tuning* tuning);

Mel_Scale mel_scale_from_indices(const Mel_Tuning* tuning, const int64_t* indices, int32_t count);

Mel_Scale mel_scale_from_pitches(const Mel_Tuning* tuning, const Mel_Pitch* pitches, int32_t count);

Mel_Scale mel_scale_copy(const Mel_Scale* s);

void mel_scale_add_pitch(Mel_Scale* s, Mel_Pitch pitch);

void mel_scale_add_index(Mel_Scale* s, int64_t index);

Mel_Pitch mel_scale_get(const Mel_Scale* s, int32_t idx);

uint8_t mel_scale_contains_pitch(const Mel_Scale* s, Mel_Pitch pitch);

uint8_t mel_scale_contains_index(const Mel_Scale* s, int64_t index);

Mel_Interval mel_scale_spec_interval(const Mel_Scale* s, int32_t source_idx, int32_t target_idx);

Mel_Scale mel_scale_transpose(const Mel_Scale* s, int64_t diff);

Mel_Scale mel_scale_union(const Mel_Scale* a, const Mel_Scale* b);

Mel_Scale mel_scale_intersection(const Mel_Scale* a, const Mel_Scale* b);

Mel_Scale mel_scale_difference(const Mel_Scale* a, const Mel_Scale* b);

uint8_t mel_scale_is_subset(const Mel_Scale* a, const Mel_Scale* b);

uint8_t mel_scale_eq(const Mel_Scale* a, const Mel_Scale* b);

Mel_Scale mel_scale_reflect(const Mel_Scale* s, Mel_Pitch axis);

Mel_Scale mel_scale_zero_normalized(const Mel_Scale* s);

void mel_scale_indices(const Mel_Scale* s, int64_t* out_indices);

Mel_Scale mel_scale_rotated_up(const Mel_Scale* s);

Mel_Scale mel_scale_rotated_down(const Mel_Scale* s);

Mel_Scale mel_scale_rotation(const Mel_Scale* s, int32_t order);

Mel_Scale mel_scale_pcs_normalized(const Mel_Scale* s);

Mel_Scale mel_scale_period_normalized(const Mel_Scale* s);

Mel_Scale mel_scale_pcs_complement(const Mel_Scale* s);

uint8_t mel_scale_is_set_equivalent(const Mel_Scale* a, const Mel_Scale* b);

typedef struct Mel_IntervalSeq Mel_IntervalSeq;
Mel_IntervalSeq mel_scale_to_interval_seq(const Mel_Scale* s);
