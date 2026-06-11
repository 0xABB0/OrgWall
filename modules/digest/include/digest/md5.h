#pragma once

#include <core/types.h>

typedef struct Mel_Md5
{
    u8 bytes[16];
} Mel_Md5;

typedef struct Mel_Md5_State
{
    u32 h[4];
    u8  buffer[64];
    u64 total_len;
} Mel_Md5_State;

Mel_Md5 mel_md5(const void* data, usize len);
void    mel_md5_init(Mel_Md5_State* st);
void    mel_md5_update(Mel_Md5_State* st, const void* data, usize len);
Mel_Md5 mel_md5_final(const Mel_Md5_State* st);
