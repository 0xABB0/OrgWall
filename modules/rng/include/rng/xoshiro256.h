#pragma once

#include <core/compiler.h>
#include <core/types.h>

#include <rng/rng.h>

typedef struct
{
    u64 s[4];
} Mel_Xoshiro256;

MEL_NODISCARD static inline Mel_Xoshiro256 mel_xoshiro256(u64 seed);

MEL_NODISCARD static inline u64 mel_xoshiro256ss_next(Mel_Xoshiro256* g);
MEL_NODISCARD static inline u64 mel_xoshiro256pp_next(Mel_Xoshiro256* g);

MEL_NODISCARD static inline Mel_Rng mel_xoshiro256ss_rng(Mel_Xoshiro256* g);
MEL_NODISCARD static inline Mel_Rng mel_xoshiro256pp_rng(Mel_Xoshiro256* g);

void mel_xoshiro256_jump(Mel_Xoshiro256* g);
void mel_xoshiro256_long_jump(Mel_Xoshiro256* g);

#include "xoshiro256.inl"
