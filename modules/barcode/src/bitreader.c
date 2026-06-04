#include <barcode/bitreader.h>

void mel_bitreader_init(mel_bitreader* r, const u8* bytes, usize n)
{
    r->bytes = bytes;
    r->n = n;
    r->pos = 0;
}

u32 mel_bitreader_get(mel_bitreader* r, u32 nbits)
{
    u32 value = 0;
    if ((r->pos & 7u) == 0 && nbits >= 8 && r->pos + nbits <= r->n * 8)
    {
        u32 nbytes = nbits >> 3;
        for (u32 b = 0; b < nbytes; ++b)
        {
            value = (value << 8) | r->bytes[(r->pos >> 3) + b];
        }
        r->pos += (usize)nbytes * 8;
        nbits &= 7u;
    }
    for (u32 i = 0; i < nbits; ++i)
    {
        u32 bit = 0;
        if (r->pos < r->n * 8)
        {
            usize byte_index = r->pos >> 3;
            u32   shift = 7u - (u32)(r->pos & 7u);
            bit = (r->bytes[byte_index] >> shift) & 1u;
            r->pos += 1;
        }
        value = (value << 1) | bit;
    }
    return value;
}

usize mel_bitreader_remaining(const mel_bitreader* r)
{
    usize total = r->n * 8;
    return r->pos >= total ? 0 : total - r->pos;
}

bool mel_bitreader_exhausted(const mel_bitreader* r) { return r->pos >= r->n * 8; }
