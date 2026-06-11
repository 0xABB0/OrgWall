#pragma once

#include <core/types.h>

typedef struct Mel_Xxh128
{
    u64 low;
    u64 high;
} Mel_Xxh128;

u64        mel_xxh64(const void* data, usize len, u64 seed);
u64        mel_xxh3_64(const void* data, usize len);
u64        mel_xxh3_64_seeded(const void* data, usize len, u64 seed);
Mel_Xxh128 mel_xxh3_128(const void* data, usize len);
Mel_Xxh128 mel_xxh3_128_seeded(const void* data, usize len, u64 seed);

typedef struct Mel_Xxh3_State
{
    u64   acc[8];
    u8    secret[192];
    u8    buffer[256];
    u64   total_len;
    u64   seed;
    usize nb_stripes_so_far;
    u32   buffered_size;
} Mel_Xxh3_State;

void       mel_xxh3_init(Mel_Xxh3_State* st);
void       mel_xxh3_init_seeded(Mel_Xxh3_State* st, u64 seed);
void       mel_xxh3_update(Mel_Xxh3_State* st, const void* data, usize len);
u64        mel_xxh3_final_64(const Mel_Xxh3_State* st);
Mel_Xxh128 mel_xxh3_final_128(const Mel_Xxh3_State* st);
