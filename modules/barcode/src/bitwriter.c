#include <barcode/bitwriter.h>

void mel_bitwriter_init(mel_bitwriter* w, const Mel_Alloc* allocator)
{
    mel_array_init(&w->buf, allocator);
    w->cur = 0;
    w->nbits = 0;
}

void mel_bitwriter_free(mel_bitwriter* w)
{
    mel_array_free(&w->buf);
    w->cur = 0;
    w->nbits = 0;
}

void mel_bitwriter_put(mel_bitwriter* w, u32 value, u32 bit_count)
{
    for (u32 i = 0; i < bit_count; ++i)
    {
        u32 bit = (value >> (bit_count - 1 - i)) & 1u;
        w->cur = (u8)((w->cur << 1) | bit);
        w->nbits += 1;
        if (w->nbits == 8)
        {
            mel_array_push(&w->buf, w->cur);
            w->cur = 0;
            w->nbits = 0;
        }
    }
}

void mel_bitwriter_pad_to_byte(mel_bitwriter* w)
{
    if (w->nbits > 0)
    {
        w->cur = (u8)(w->cur << (8 - w->nbits));
        mel_array_push(&w->buf, w->cur);
        w->cur = 0;
        w->nbits = 0;
    }
}

usize mel_bitwriter_bit_length(const mel_bitwriter* w) { return w->buf.count * 8 + w->nbits; }

const u8* mel_bitwriter_bytes(const mel_bitwriter* w) { return w->buf.items; }

usize mel_bitwriter_byte_count(const mel_bitwriter* w) { return w->buf.count; }
