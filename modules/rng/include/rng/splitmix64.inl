#ifdef _CLANGD
#pragma once
#include "splitmix64.h"
#endif

static inline Mel_SplitMix64 mel_splitmix64(u64 seed) { return (Mel_SplitMix64){ .state = seed }; }

static inline u64 mel_splitmix64_next(Mel_SplitMix64* g)
{
    u64 z = (g->state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static inline u64 mel__splitmix64_thunk(void* state) { return mel_splitmix64_next((Mel_SplitMix64*)state); }

static inline Mel_Rng mel_splitmix64_rng(Mel_SplitMix64* g) { return (Mel_Rng){ .next = mel__splitmix64_thunk, .state = g }; }
