#pragma once

#include <core/types.h>

typedef struct mel_bitreader
{
    const u8* bytes;
    usize     n;
    usize     pos;
} mel_bitreader;

void mel_bitreader_init(mel_bitreader* r, const u8* bytes, usize n);

u32   mel_bitreader_get(mel_bitreader* r, u32 nbits);
usize mel_bitreader_remaining(const mel_bitreader* r);
bool  mel_bitreader_exhausted(const mel_bitreader* r);
