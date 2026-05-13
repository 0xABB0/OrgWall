#pragma once

#include <stdint.h>
#include <mpfr.h>
#include "tuning.h"
#include "../time.frequency/frequency.h"

typedef struct Mel_Pitch Mel_Pitch;

struct Mel_Pitch
{
  const Mel_Tuning* tuning;
  Mel_Hz frequency;
  int64_t pitch_index;
};

Mel_Pitch mel_pitch_make(const Mel_Tuning* tuning, int64_t pitch_index);

Mel_Pitch mel_pitch_copy(Mel_Pitch pitch);

Mel_Pitch mel_pitch_transpose(Mel_Pitch pitch, int64_t diff);

Mel_Pitch mel_pitch_retune(Mel_Pitch pitch, const Mel_Tuning* target_tuning);

int64_t mel_pitch_pc_index(Mel_Pitch p);
int64_t mel_pitch_bi_index(Mel_Pitch p);

Mel_Pitch mel_pitch_transpose_bi(Mel_Pitch p, int64_t bi_diff);

Mel_Pitch mel_pitch_pcs_normalized(Mel_Pitch p);

uint8_t mel_pitch_is_equivalent(Mel_Pitch a, Mel_Pitch b);

int64_t mel_pitch_generator_distance(Mel_Pitch pitch, Mel_Pitch generator);

uint8_t mel_pitch_eq(Mel_Pitch a, Mel_Pitch b);
uint8_t mel_pitch_cmp(Mel_Pitch a, Mel_Pitch b);

static inline int mel_pitch_cmp_int(Mel_Pitch a, Mel_Pitch b)
{
  uint8_t c = mel_pitch_cmp(a, b);
  if (c == 0) return -1;
  if (c == 1) return 0;
  return 1;
}
