#pragma once

#include <core/types.h>

typedef struct Mel_Sm3
{
    u8 bytes[32];
} Mel_Sm3;

typedef struct Mel_Sm3_State
{
    u32 h[8];
    u8  buffer[64];
    u64 total_len;
} Mel_Sm3_State;

Mel_Sm3 mel_sm3(const void* data, usize len);
void    mel_sm3_init(Mel_Sm3_State* st);
void    mel_sm3_update(Mel_Sm3_State* st, const void* data, usize len);
Mel_Sm3 mel_sm3_final(const Mel_Sm3_State* st);
