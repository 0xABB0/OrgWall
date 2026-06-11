#pragma once

#include <core/types.h>

u64 mel_siphash24(const void* data, usize len, u64 k0, u64 k1);
