#pragma once

#include <core/types.h>

typedef struct Mel_Sha1
{
    u8 bytes[20];
} Mel_Sha1;

typedef struct Mel_Sha1_State
{
    u32 h[5];
    u8  buffer[64];
    u64 total_len;
} Mel_Sha1_State;

Mel_Sha1 mel_sha1(const void* data, usize len);
void     mel_sha1_init(Mel_Sha1_State* st);
void     mel_sha1_update(Mel_Sha1_State* st, const void* data, usize len);
Mel_Sha1 mel_sha1_final(const Mel_Sha1_State* st);
