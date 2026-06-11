#pragma once

#include <core/types.h>

typedef struct Mel_Sha3_224
{
    u8 bytes[28];
} Mel_Sha3_224;

typedef struct Mel_Sha3_256
{
    u8 bytes[32];
} Mel_Sha3_256;

typedef struct Mel_Sha3_384
{
    u8 bytes[48];
} Mel_Sha3_384;

typedef struct Mel_Sha3_512
{
    u8 bytes[64];
} Mel_Sha3_512;

typedef struct Mel_Sha3_State
{
    u64 a[25];
    u32 rate;
    u32 offset;
} Mel_Sha3_State;

typedef struct Mel_Shake_State
{
    u64 a[25];
    u32 rate;
    u32 offset;
    u32 squeezing;
} Mel_Shake_State;

Mel_Sha3_224 mel_sha3_224(const void* data, usize len);
Mel_Sha3_256 mel_sha3_256(const void* data, usize len);
Mel_Sha3_384 mel_sha3_384(const void* data, usize len);
Mel_Sha3_512 mel_sha3_512(const void* data, usize len);

void         mel_sha3_224_init(Mel_Sha3_State* st);
void         mel_sha3_256_init(Mel_Sha3_State* st);
void         mel_sha3_384_init(Mel_Sha3_State* st);
void         mel_sha3_512_init(Mel_Sha3_State* st);
void         mel_sha3_update(Mel_Sha3_State* st, const void* data, usize len);
Mel_Sha3_224 mel_sha3_224_final(const Mel_Sha3_State* st);
Mel_Sha3_256 mel_sha3_256_final(const Mel_Sha3_State* st);
Mel_Sha3_384 mel_sha3_384_final(const Mel_Sha3_State* st);
Mel_Sha3_512 mel_sha3_512_final(const Mel_Sha3_State* st);

void mel_shake128(const void* data, usize len, void* out, usize out_len);
void mel_shake256(const void* data, usize len, void* out, usize out_len);

void mel_shake128_init(Mel_Shake_State* st);
void mel_shake256_init(Mel_Shake_State* st);
void mel_shake_update(Mel_Shake_State* st, const void* data, usize len);
void mel_shake_squeeze(Mel_Shake_State* st, void* out, usize out_len);
