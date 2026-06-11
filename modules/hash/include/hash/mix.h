#pragma once

#include <core/types.h>

static inline u32 mel_hash_mix32(u32 x)
{
    x ^= x >> 16;
    x *= 0x85EBCA6BU;
    x ^= x >> 13;
    x *= 0xC2B2AE35U;
    x ^= x >> 16;
    return x;
}

static inline u64 mel_hash_mix64(u64 x)
{
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

static inline u64 mel_hash_combine64(u64 seed, u64 value) { return mel_hash_mix64(seed ^ (value + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2))); }
