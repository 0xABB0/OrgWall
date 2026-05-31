#pragma once

#include <core/compiler.h>
#include <core/types.h>

#include <rng/rng.h>

typedef struct
{
    u64 state;
    u64 inc;
} Mel_Pcg32;

MEL_NODISCARD static inline Mel_Pcg32 mel_pcg32(u64 seed, u64 stream);
MEL_NODISCARD static inline u32       mel_pcg32_next(Mel_Pcg32* g);
MEL_NODISCARD static inline Mel_Rng   mel_pcg32_rng(Mel_Pcg32* g);

#include "pcg32.inl"
