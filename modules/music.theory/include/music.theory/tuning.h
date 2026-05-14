#pragma once

#include <core/compiler.h>

#include <stdint.h>
#include <mpfr.h>
#include <gmp.h>
#include <time.frequency/frequency.h>

typedef struct Mel_Tuning Mel_Tuning;

typedef enum
{
  MEL_TUNING_ED = 0,
  MEL_TUNING_EDO,
  MEL_TUNING_CUSTOM,
} Mel_Tuning_Kind;

struct Mel_Tuning
{
  Mel_Tuning_Kind kind;
  Mel_Hz ref_frequency;
  union
  {
    struct
    {
      uint32_t divisions;
      mpq_t eq_ratio;
    } ed;
    struct
    {
      uint32_t divisions;
    } edo;
    struct
    {
      uint32_t steps_count;
      mpfr_t eq_ratio;
      mpfr_t* steps;
    } custom;
  };
};

void mel_tuning_free(Mel_Tuning* t);
static inline void mel_tuning_cleanup(Mel_Tuning* t) { mel_tuning_free(t); }
#define Mel_Tuning_AUTO MEL_CLEANUP(mel_tuning_cleanup) Mel_Tuning

Mel_Tuning mel_tuning_ed(uint32_t divisions, uint64_t eq_num, uint64_t eq_den, Mel_Hz ref_frequency);
Mel_Tuning mel_tuning_edo(uint32_t divisions, Mel_Hz ref_frequency);

Mel_Tuning mel_tuning_custom(uint32_t steps_count, Mel_Hz ref_frequency);
void mel_tuning_custom_set_step(Mel_Tuning* t, uint32_t idx, mpfr_srcptr ratio);
void mel_tuning_custom_set_step_rational(Mel_Tuning* t, uint32_t idx, uint64_t num, uint64_t den);
void mel_tuning_custom_set_eq_ratio(Mel_Tuning* t, mpfr_srcptr ratio);

Mel_Hz mel_tuning_frequency_for_index(const Mel_Tuning* t, int64_t pitch_index);

int64_t mel_tuning_find_index(const Mel_Tuning* t, Mel_Hz frequency);

uint32_t mel_tuning_period_length(const Mel_Tuning* t);
uint8_t mel_tuning_is_periodic(const Mel_Tuning* t);

int32_t mel_tuning_get_generators(const Mel_Tuning* t, int64_t* out_indices, int32_t max_count);
