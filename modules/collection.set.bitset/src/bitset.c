#include "bitset.h"
#include "allocator.h"

#include <string.h>

#define BITS_PER_WORD 64

static usize bits_to_words(usize bit_count)
{
    return (bit_count + BITS_PER_WORD - 1) / BITS_PER_WORD;
}

static u64 tail_mask(usize bit_count)
{
    usize tail = bit_count % BITS_PER_WORD;
    if (tail == 0) return ~(u64)0;
    return ((u64)1 << tail) - 1;
}

void mel_bitset_init(Mel_BitSet* bs, usize bit_count, const Mel_Alloc* alloc)
{
    bs->allocator = alloc;
    bs->bit_count = bit_count;
    bs->word_count = bits_to_words(bit_count);
    if (bs->word_count > 0)
        bs->words = (u64*)mel_calloc(alloc, bs->word_count * sizeof(u64));
    else
        bs->words = nullptr;
}

void mel_bitset_free(Mel_BitSet* bs)
{
    if (bs->words)
        mel_dealloc(bs->allocator, bs->words);
    bs->words = nullptr;
    bs->bit_count = 0;
    bs->word_count = 0;
}

void mel_bitset_set(Mel_BitSet* bs, usize index)
{
    assert(index < bs->bit_count);
    bs->words[index / BITS_PER_WORD] |= (u64)1 << (index % BITS_PER_WORD);
}

void mel_bitset_clear_bit(Mel_BitSet* bs, usize index)
{
    assert(index < bs->bit_count);
    bs->words[index / BITS_PER_WORD] &= ~((u64)1 << (index % BITS_PER_WORD));
}

bool mel_bitset_get(const Mel_BitSet* bs, usize index)
{
    assert(index < bs->bit_count);
    return (bs->words[index / BITS_PER_WORD] >> (index % BITS_PER_WORD)) & 1;
}

void mel_bitset_toggle(Mel_BitSet* bs, usize index)
{
    assert(index < bs->bit_count);
    bs->words[index / BITS_PER_WORD] ^= (u64)1 << (index % BITS_PER_WORD);
}

void mel_bitset_clear(Mel_BitSet* bs)
{
    if (bs->word_count > 0)
        memset(bs->words, 0, bs->word_count * sizeof(u64));
}

void mel_bitset_set_all(Mel_BitSet* bs)
{
    if (bs->word_count == 0) return;
    memset(bs->words, 0xFF, bs->word_count * sizeof(u64));
    bs->words[bs->word_count - 1] &= tail_mask(bs->bit_count);
}

void mel_bitset_resize(Mel_BitSet* bs, usize new_bit_count)
{
    usize new_word_count = bits_to_words(new_bit_count);

    if (new_word_count == 0)
    {
        if (bs->words)
            mel_dealloc(bs->allocator, bs->words);
        bs->words = nullptr;
        bs->bit_count = new_bit_count;
        bs->word_count = 0;
        return;
    }

    if (new_word_count != bs->word_count)
    {
        if (bs->words)
        {
            u64* new_words = (u64*)mel_calloc(bs->allocator, new_word_count * sizeof(u64));
            usize copy_count = new_word_count < bs->word_count ? new_word_count : bs->word_count;
            memcpy(new_words, bs->words, copy_count * sizeof(u64));
            mel_dealloc(bs->allocator, bs->words);
            bs->words = new_words;
        }
        else
        {
            bs->words = (u64*)mel_calloc(bs->allocator, new_word_count * sizeof(u64));
        }
    }

    if (new_bit_count < bs->bit_count && new_word_count > 0)
        bs->words[new_word_count - 1] &= tail_mask(new_bit_count);

    bs->bit_count = new_bit_count;
    bs->word_count = new_word_count;
}

usize mel_bitset_count_set(const Mel_BitSet* bs)
{
    usize count = 0;
    for (usize i = 0; i < bs->word_count; i++)
        count += (usize)__builtin_popcountll(bs->words[i]);
    return count;
}

usize mel_bitset_count_clear(const Mel_BitSet* bs)
{
    return bs->bit_count - mel_bitset_count_set(bs);
}

bool mel_bitset_any(const Mel_BitSet* bs)
{
    for (usize i = 0; i < bs->word_count; i++)
    {
        if (bs->words[i] != 0)
            return true;
    }
    return false;
}

bool mel_bitset_none(const Mel_BitSet* bs)
{
    return !mel_bitset_any(bs);
}

bool mel_bitset_all(const Mel_BitSet* bs)
{
    if (bs->bit_count == 0) return true;

    for (usize i = 0; i + 1 < bs->word_count; i++)
    {
        if (bs->words[i] != ~(u64)0)
            return false;
    }

    return bs->words[bs->word_count - 1] == tail_mask(bs->bit_count);
}

void mel_bitset_and(Mel_BitSet* dst, const Mel_BitSet* a, const Mel_BitSet* b)
{
    assert(dst->word_count == a->word_count && a->word_count == b->word_count);
    for (usize i = 0; i < dst->word_count; i++)
        dst->words[i] = a->words[i] & b->words[i];
}

void mel_bitset_or(Mel_BitSet* dst, const Mel_BitSet* a, const Mel_BitSet* b)
{
    assert(dst->word_count == a->word_count && a->word_count == b->word_count);
    for (usize i = 0; i < dst->word_count; i++)
        dst->words[i] = a->words[i] | b->words[i];
}

void mel_bitset_xor(Mel_BitSet* dst, const Mel_BitSet* a, const Mel_BitSet* b)
{
    assert(dst->word_count == a->word_count && a->word_count == b->word_count);
    for (usize i = 0; i < dst->word_count; i++)
        dst->words[i] = a->words[i] ^ b->words[i];
}

void mel_bitset_not(Mel_BitSet* dst, const Mel_BitSet* src)
{
    assert(dst->word_count == src->word_count);
    for (usize i = 0; i < dst->word_count; i++)
        dst->words[i] = ~src->words[i];
    if (dst->word_count > 0)
        dst->words[dst->word_count - 1] &= tail_mask(dst->bit_count);
}

usize mel_bitset_first_set(const Mel_BitSet* bs)
{
    for (usize i = 0; i < bs->word_count; i++)
    {
        if (bs->words[i] != 0)
        {
            usize bit = i * BITS_PER_WORD + (usize)__builtin_ctzll(bs->words[i]);
            return bit < bs->bit_count ? bit : bs->bit_count;
        }
    }
    return bs->bit_count;
}

usize mel_bitset_first_clear(const Mel_BitSet* bs)
{
    for (usize i = 0; i < bs->word_count; i++)
    {
        u64 mask = (i == bs->word_count - 1) ? tail_mask(bs->bit_count) : ~(u64)0;
        u64 inverted = ~bs->words[i] & mask;
        if (inverted != 0)
        {
            usize bit = i * BITS_PER_WORD + (usize)__builtin_ctzll(inverted);
            return bit < bs->bit_count ? bit : bs->bit_count;
        }
    }
    return bs->bit_count;
}
