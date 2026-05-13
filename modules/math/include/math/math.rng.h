#pragma once

#include <core/compiler.h>

#include "core.types.h"

typedef struct
{
    u64 state;
} Mel_Rng;

MEL_NODISCARD static inline Mel_Rng mel_rng(u64 seed);
MEL_NODISCARD static inline u64 mel_rng_next(Mel_Rng* rng);
MEL_NODISCARD static inline u32 mel_rng_next32(Mel_Rng* rng);
MEL_NODISCARD static inline u32 mel_rng_bounded(Mel_Rng* rng, u32 bound);
MEL_NODISCARD static inline f32 mel_rng_f32(Mel_Rng* rng);

#include "math.rng.inl"
