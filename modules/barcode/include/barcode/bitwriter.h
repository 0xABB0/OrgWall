#pragma once

#include <collection.array/array.h>
#include <core/types.h>

typedef struct mel_bitwriter {
    Mel_Array(u8) buf;
    u8 cur;
    u32 nbits;
} mel_bitwriter;

void mel_bitwriter_init(mel_bitwriter* w, const Mel_Alloc* allocator);
void mel_bitwriter_free(mel_bitwriter* w);

void mel_bitwriter_put(mel_bitwriter* w, u32 value, u32 bit_count);
void mel_bitwriter_pad_to_byte(mel_bitwriter* w);

usize mel_bitwriter_bit_length(const mel_bitwriter* w);
const u8* mel_bitwriter_bytes(const mel_bitwriter* w);
usize mel_bitwriter_byte_count(const mel_bitwriter* w);
