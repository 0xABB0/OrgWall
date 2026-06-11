#pragma once

#include <core/types.h>

typedef struct Mel_Blake2b_State
{
    u64   h[8];
    u64   t[2];
    u8    buffer[128];
    usize buffered;
    usize out_len;
} Mel_Blake2b_State;

typedef struct Mel_Blake2s_State
{
    u32   h[8];
    u32   t[2];
    u8    buffer[64];
    usize buffered;
    usize out_len;
} Mel_Blake2s_State;

void mel_blake2b(void* out, usize out_len, const void* data, usize len, const void* key, usize key_len);
void mel_blake2b_init(Mel_Blake2b_State* st, usize out_len, const void* key, usize key_len);
void mel_blake2b_update(Mel_Blake2b_State* st, const void* data, usize len);
void mel_blake2b_final(const Mel_Blake2b_State* st, void* out);

void mel_blake2s(void* out, usize out_len, const void* data, usize len, const void* key, usize key_len);
void mel_blake2s_init(Mel_Blake2s_State* st, usize out_len, const void* key, usize key_len);
void mel_blake2s_update(Mel_Blake2s_State* st, const void* data, usize len);
void mel_blake2s_final(const Mel_Blake2s_State* st, void* out);
