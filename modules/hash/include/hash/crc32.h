#pragma once

#include <core/types.h>

u32 mel_crc32(const void* data, usize len);
u32 mel_crc32_update(u32 crc, const void* data, usize len);
u32 mel_crc32c(const void* data, usize len);
u32 mel_crc32c_update(u32 crc, const void* data, usize len);
