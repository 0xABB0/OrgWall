#include "pitch.h"
#include <stdlib.h>

Mel_Pitch mel_pitch_make(const Mel_Tuning* tuning, int64_t pitch_index)
{
  Mel_Pitch p;
  p.tuning = tuning;
  p.pitch_index = pitch_index;
  p.frequency = mel_tuning_frequency_for_index(tuning, pitch_index);
  return p;
}

Mel_Pitch mel_pitch_copy(Mel_Pitch pitch)
{
  return pitch;
}

Mel_Pitch mel_pitch_transpose(Mel_Pitch pitch, int64_t diff)
{
  return mel_pitch_make(pitch.tuning, pitch.pitch_index + diff);
}

Mel_Pitch mel_pitch_retune(Mel_Pitch pitch, const Mel_Tuning* target_tuning)
{
  int64_t new_index = mel_tuning_find_index(target_tuning, pitch.frequency);
  return mel_pitch_make(target_tuning, new_index);
}

int64_t mel_pitch_pc_index(Mel_Pitch p)
{
  uint32_t period = mel_tuning_period_length(p.tuning);
  if (period == 0) return 0;
  int64_t pc = p.pitch_index % (int64_t)period;
  if (pc < 0) pc += period;
  return pc;
}

int64_t mel_pitch_bi_index(Mel_Pitch p)
{
  uint32_t period = mel_tuning_period_length(p.tuning);
  if (period == 0) return 0;
  return p.pitch_index / (int64_t)period;
}

Mel_Pitch mel_pitch_transpose_bi(Mel_Pitch p, int64_t bi_diff)
{
  uint32_t period = mel_tuning_period_length(p.tuning);
  int64_t pc = mel_pitch_pc_index(p);
  int64_t bi = mel_pitch_bi_index(p) + bi_diff;
  return mel_pitch_make(p.tuning, pc + bi * period);
}

Mel_Pitch mel_pitch_pcs_normalized(Mel_Pitch p)
{
  return mel_pitch_transpose_bi(p, -mel_pitch_bi_index(p));
}

uint8_t mel_pitch_is_equivalent(Mel_Pitch a, Mel_Pitch b)
{
  if (a.tuning == b.tuning)
    return mel_pitch_pc_index(a) == mel_pitch_pc_index(b) ? 1 : 0;
  return mel_pitch_eq(mel_pitch_pcs_normalized(a), mel_pitch_pcs_normalized(b));
}

int64_t mel_pitch_generator_distance(Mel_Pitch pitch, Mel_Pitch generator)
{
  if (pitch.tuning != generator.tuning) return -1;

  uint32_t period = mel_tuning_period_length(pitch.tuning);
  if (period == 0) return -1;

  int64_t gen_pc = mel_pitch_pc_index(generator);
  if (gen_pc == 0) return -1;

  int64_t pc = 0;
  int64_t dist = 0;
  int64_t target_pc = mel_pitch_pc_index(pitch);

  while (1)
  {
    if (pc == target_pc) break;
    dist++;
    pc = (pc + gen_pc) % (int64_t)period;
    if (dist > (int64_t)period) return -1;
  }

  int64_t alt_dist = (int64_t)period - dist;
  return dist < alt_dist ? dist : alt_dist;
}

uint8_t mel_pitch_eq(Mel_Pitch a, Mel_Pitch b)
{
  if (a.tuning == b.tuning) return a.pitch_index == b.pitch_index ? 1 : 0;
  return mel_freq_eq(a.frequency, b.frequency);
}

uint8_t mel_pitch_cmp(Mel_Pitch a, Mel_Pitch b)
{
  if (a.tuning == b.tuning)
  {
    if (a.pitch_index < b.pitch_index) return 0;
    if (a.pitch_index > b.pitch_index) return 2;
    return 1;
  }
  return mel_freq_cmp(a.frequency, b.frequency);
}
