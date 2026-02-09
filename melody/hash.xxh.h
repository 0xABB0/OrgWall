#pragma once

#include "types.h"

u64 mel_xxh64(const void* data, usize len, u64 seed);
u64 mel_xxh3_64(const void* data, usize len);
u64 mel_xxh3_64_seeded(const void* data, usize len, u64 seed);
