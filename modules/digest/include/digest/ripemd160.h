#pragma once

#include <core/types.h>

typedef struct Mel_Ripemd160
{
    u8 bytes[20];
} Mel_Ripemd160;

typedef struct Mel_Ripemd160_State
{
    u32 h[5];
    u8  buffer[64];
    u64 total_len;
} Mel_Ripemd160_State;

Mel_Ripemd160 mel_ripemd160(const void* data, usize len);
void          mel_ripemd160_init(Mel_Ripemd160_State* st);
void          mel_ripemd160_update(Mel_Ripemd160_State* st, const void* data, usize len);
Mel_Ripemd160 mel_ripemd160_final(const Mel_Ripemd160_State* st);
