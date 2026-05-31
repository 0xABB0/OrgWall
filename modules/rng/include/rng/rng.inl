#ifdef _CLANGD
#pragma once
#include "rng.h"
#endif

static inline u64 mel_rng_next(Mel_Rng rng) { return rng.next(rng.state); }

static inline u32 mel_rng_u32(Mel_Rng rng) { return (u32)(mel_rng_next(rng) >> 32); }

static inline bool mel_rng_bool(Mel_Rng rng) { return (bool)(mel_rng_next(rng) >> 63); }

static inline f32 mel_rng_f32(Mel_Rng rng) { return (f32)(u32)(mel_rng_next(rng) >> 40) * (1.0f / 16777216.0f); }

static inline f64 mel_rng_f64(Mel_Rng rng) { return (f64)(mel_rng_next(rng) >> 11) * (1.0 / 9007199254740992.0); }
