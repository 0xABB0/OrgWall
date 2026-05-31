#ifdef _CLANGD
#pragma once
#include "pcg32.h"
#endif

static inline u32 mel_pcg32_next(Mel_Pcg32* g)
{
    u64 old = g->state;
    g->state = old * 6364136223846793005ull + g->inc;
    u32 xsh = (u32)(((old >> 18u) ^ old) >> 27u);
    u32 rot = (u32)(old >> 59u);
    return (xsh >> rot) | (xsh << ((0u - rot) & 31u));
}

static inline Mel_Pcg32 mel_pcg32(u64 seed, u64 stream)
{
    Mel_Pcg32 g = { .state = 0u, .inc = (stream << 1u) | 1u };
    (void)mel_pcg32_next(&g);
    g.state += seed;
    (void)mel_pcg32_next(&g);
    return g;
}

static inline u64 mel__pcg32_thunk(void* state)
{
    Mel_Pcg32* g = (Mel_Pcg32*)state;
    u64        hi = mel_pcg32_next(g);
    u64        lo = mel_pcg32_next(g);
    return (hi << 32) | lo;
}

static inline Mel_Rng mel_pcg32_rng(Mel_Pcg32* g) { return (Mel_Rng){ .next = mel__pcg32_thunk, .state = g }; }
