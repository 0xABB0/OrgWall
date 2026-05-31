#ifdef _CLANGD
#pragma once
#include "xoshiro256.h"
#endif

#include <rng/splitmix64.h>

static inline u64 mel__xoshiro256_rotl(u64 x, int k) { return (x << k) | (x >> (64 - k)); }

static inline Mel_Xoshiro256 mel_xoshiro256(u64 seed)
{
    Mel_SplitMix64 sm = mel_splitmix64(seed);
    Mel_Xoshiro256 g;
    g.s[0] = mel_splitmix64_next(&sm);
    g.s[1] = mel_splitmix64_next(&sm);
    g.s[2] = mel_splitmix64_next(&sm);
    g.s[3] = mel_splitmix64_next(&sm);
    return g;
}

static inline void mel__xoshiro256_advance(u64* s)
{
    u64 t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = mel__xoshiro256_rotl(s[3], 45);
}

static inline u64 mel_xoshiro256ss_next(Mel_Xoshiro256* g)
{
    u64 result = mel__xoshiro256_rotl(g->s[1] * 5, 7) * 9;
    mel__xoshiro256_advance(g->s);
    return result;
}

static inline u64 mel_xoshiro256pp_next(Mel_Xoshiro256* g)
{
    u64 result = mel__xoshiro256_rotl(g->s[0] + g->s[3], 23) + g->s[0];
    mel__xoshiro256_advance(g->s);
    return result;
}

static inline u64 mel__xoshiro256ss_thunk(void* state) { return mel_xoshiro256ss_next((Mel_Xoshiro256*)state); }

static inline u64 mel__xoshiro256pp_thunk(void* state) { return mel_xoshiro256pp_next((Mel_Xoshiro256*)state); }

static inline Mel_Rng mel_xoshiro256ss_rng(Mel_Xoshiro256* g) { return (Mel_Rng){ .next = mel__xoshiro256ss_thunk, .state = g }; }

static inline Mel_Rng mel_xoshiro256pp_rng(Mel_Xoshiro256* g) { return (Mel_Rng){ .next = mel__xoshiro256pp_thunk, .state = g }; }
