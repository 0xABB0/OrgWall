#pragma once

#include "core.types.h"
#include "allocator.fwd.h"

typedef struct Mel_BitSet Mel_BitSet;
struct Mel_BitSet
{
    u64* words;
    usize bit_count;
    usize word_count;
    const Mel_Alloc* allocator;
};

void  mel_bitset_init(Mel_BitSet* bs, usize bit_count, const Mel_Alloc* alloc);
void  mel_bitset_free(Mel_BitSet* bs);

void  mel_bitset_set(Mel_BitSet* bs, usize index);
void  mel_bitset_clear_bit(Mel_BitSet* bs, usize index);
bool  mel_bitset_get(const Mel_BitSet* bs, usize index);
void  mel_bitset_toggle(Mel_BitSet* bs, usize index);

void  mel_bitset_clear(Mel_BitSet* bs);
void  mel_bitset_set_all(Mel_BitSet* bs);

void  mel_bitset_resize(Mel_BitSet* bs, usize new_bit_count);

usize mel_bitset_count_set(const Mel_BitSet* bs);
usize mel_bitset_count_clear(const Mel_BitSet* bs);

bool  mel_bitset_any(const Mel_BitSet* bs);
bool  mel_bitset_none(const Mel_BitSet* bs);
bool  mel_bitset_all(const Mel_BitSet* bs);

void  mel_bitset_and(Mel_BitSet* dst, const Mel_BitSet* a, const Mel_BitSet* b);
void  mel_bitset_or(Mel_BitSet* dst, const Mel_BitSet* a, const Mel_BitSet* b);
void  mel_bitset_xor(Mel_BitSet* dst, const Mel_BitSet* a, const Mel_BitSet* b);
void  mel_bitset_not(Mel_BitSet* dst, const Mel_BitSet* src);

usize mel_bitset_first_set(const Mel_BitSet* bs);
usize mel_bitset_first_clear(const Mel_BitSet* bs);
