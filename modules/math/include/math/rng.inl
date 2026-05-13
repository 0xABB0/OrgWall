#ifdef _CLANGD
#pragma once
#include "rng.h"
#endif

static inline Mel_Rng mel_rng(u64 seed)
{
    if (seed == 0) seed = 42;
    return (Mel_Rng){ .state = seed };
}

static inline u64 mel_rng_next(Mel_Rng* rng)
{
    rng->state ^= rng->state << 13;
    rng->state ^= rng->state >> 7;
    rng->state ^= rng->state << 17;
    return rng->state;
}

static inline u32 mel_rng_next32(Mel_Rng* rng)
{
    return (u32)(mel_rng_next(rng) & 0xFFFFFFFF);
}

static inline u32 mel_rng_bounded(Mel_Rng* rng, u32 bound)
{
    return mel_rng_next32(rng) % bound;
}

static inline f32 mel_rng_f32(Mel_Rng* rng)
{
    return (f32)(mel_rng_next(rng) & 0xFFFFFF) / (f32)0xFFFFFF;
}
