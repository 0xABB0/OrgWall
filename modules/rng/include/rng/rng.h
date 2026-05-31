#pragma once

#include <core/compiler.h>
#include <core/types.h>

typedef u64 (*Mel_Rng_Next_Fn)(void* state);

typedef struct
{
    Mel_Rng_Next_Fn next;
    void*           state;
} Mel_Rng;

MEL_NODISCARD static inline u64  mel_rng_next(Mel_Rng rng);
MEL_NODISCARD static inline u32  mel_rng_u32(Mel_Rng rng);
MEL_NODISCARD static inline bool mel_rng_bool(Mel_Rng rng);
MEL_NODISCARD static inline f32  mel_rng_f32(Mel_Rng rng);
MEL_NODISCARD static inline f64  mel_rng_f64(Mel_Rng rng);

MEL_NODISCARD u32   mel_rng_below_u32(Mel_Rng rng, u32 bound);
MEL_NODISCARD u64   mel_rng_below_u64(Mel_Rng rng, u64 bound);
MEL_NODISCARD usize mel_rng_index(Mel_Rng rng, usize count);
MEL_NODISCARD i32   mel_rng_range_i32(Mel_Rng rng, i32 lo, i32 hi);
MEL_NODISCARD i64   mel_rng_range_i64(Mel_Rng rng, i64 lo, i64 hi);
MEL_NODISCARD f32   mel_rng_range_f32(Mel_Rng rng, f32 lo, f32 hi);
MEL_NODISCARD f64   mel_rng_range_f64(Mel_Rng rng, f64 lo, f64 hi);
MEL_NODISCARD bool  mel_rng_chance(Mel_Rng rng, f64 probability);

MEL_NODISCARD f64 mel_rng_normal(Mel_Rng rng);
MEL_NODISCARD f64 mel_rng_normal_about(Mel_Rng rng, f64 mean, f64 stddev);
MEL_NODISCARD f64 mel_rng_exponential(Mel_Rng rng, f64 rate);

void mel_rng_fill(Mel_Rng rng, void* dst, usize bytes);
void mel_rng_shuffle(Mel_Rng rng, void* base, usize count, usize size);

#include "rng.inl"
