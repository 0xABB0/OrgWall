#pragma once

#include <core/compiler.h>
#include <core/types.h>

#include <rng/pcg32.h>
#include <rng/xoshiro256.h>

MEL_NODISCARD bool mel_rng_entropy(void* dst, usize bytes);
MEL_NODISCARD u64  mel_rng_entropy_u64(void);

MEL_NODISCARD Mel_Pcg32      mel_pcg32_seeded(void);
MEL_NODISCARD Mel_Xoshiro256 mel_xoshiro256_seeded(void);
