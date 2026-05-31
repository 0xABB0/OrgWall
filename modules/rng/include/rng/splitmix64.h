#pragma once

#include <core/compiler.h>
#include <core/types.h>

#include <rng/rng.h>

typedef struct
{
    u64 state;
} Mel_SplitMix64;

MEL_NODISCARD static inline Mel_SplitMix64 mel_splitmix64(u64 seed);
MEL_NODISCARD static inline u64            mel_splitmix64_next(Mel_SplitMix64* g);
MEL_NODISCARD static inline Mel_Rng        mel_splitmix64_rng(Mel_SplitMix64* g);

#include "splitmix64.inl"
