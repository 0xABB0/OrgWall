#pragma once

#include <mpfr.h>

#include <core/compiler.h>
#include <core/types.h>
#include <math/real.h>
#include <frequency/frequency.h>
#include <allocator/allocator.h>

typedef struct Mel_Tuning Mel_Tuning;

typedef Mel_Hz (*Mel_Tuning_Frequency_Fn)(const void* ctx, Mel_Hz ref, i64 index);
typedef void (*Mel_Tuning_Destroy_Fn)(void* ctx, const Mel_Alloc* alloc);

struct Mel_Tuning
{
    void*                   ctx;
    const Mel_Alloc*        alloc;
    Mel_Hz                  ref_frequency;
    u32                     period;
    Mel_Tuning_Frequency_Fn frequency_for_index;
    Mel_Tuning_Destroy_Fn   destroy;
};

void               mel_tuning_free(Mel_Tuning* t);
static inline void mel_tuning_cleanup(Mel_Tuning* t) { mel_tuning_free(t); }
#define Mel_Tuning_AUTO MEL_CLEANUP(mel_tuning_cleanup) Mel_Tuning

Mel_Tuning mel_tuning_ed(const Mel_Alloc* alloc, u32 divisions, u64 eq_num, u64 eq_den, Mel_Hz ref_frequency);
Mel_Tuning mel_tuning_edo(const Mel_Alloc* alloc, u32 divisions, Mel_Hz ref_frequency);

Mel_Tuning mel_tuning_custom(const Mel_Alloc* alloc, u32 steps_count, Mel_Hz ref_frequency);
void       mel_tuning_custom_set_step(Mel_Tuning* t, u32 idx, mpfr_srcptr ratio);
void       mel_tuning_custom_set_step_rational(Mel_Tuning* t, u32 idx, u64 num, u64 den);
void       mel_tuning_custom_set_eq_ratio(Mel_Tuning* t, mpfr_srcptr ratio);

MEL_NODISCARD Mel_Hz mel_tuning_frequency_for_index(const Mel_Tuning* t, i64 pitch_index);

MEL_NODISCARD i64 mel_tuning_find_index(const Mel_Tuning* t, Mel_Hz frequency);

MEL_NODISCARD static inline u32  mel_tuning_period_length(const Mel_Tuning* t) { return t->period; }
MEL_NODISCARD static inline bool mel_tuning_is_periodic(const Mel_Tuning* t) { return t->period > 0; }

i32 mel_tuning_get_generators(const Mel_Tuning* t, i64* out_indices, i32 max_count);
