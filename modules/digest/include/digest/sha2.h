#pragma once

#include <core/types.h>

typedef struct Mel_Sha224
{
    u8 bytes[28];
} Mel_Sha224;

typedef struct Mel_Sha256
{
    u8 bytes[32];
} Mel_Sha256;

typedef struct Mel_Sha384
{
    u8 bytes[48];
} Mel_Sha384;

typedef struct Mel_Sha512
{
    u8 bytes[64];
} Mel_Sha512;

typedef struct Mel_Sha512_224
{
    u8 bytes[28];
} Mel_Sha512_224;

typedef struct Mel_Sha512_256
{
    u8 bytes[32];
} Mel_Sha512_256;

typedef struct Mel_Sha256_State
{
    u32 h[8];
    u8  buffer[64];
    u64 total_len;
} Mel_Sha256_State;

typedef struct Mel_Sha512_State
{
    u64 h[8];
    u8  buffer[128];
    u64 total_len;
} Mel_Sha512_State;

Mel_Sha224     mel_sha224(const void* data, usize len);
Mel_Sha256     mel_sha256(const void* data, usize len);
Mel_Sha384     mel_sha384(const void* data, usize len);
Mel_Sha512     mel_sha512(const void* data, usize len);
Mel_Sha512_224 mel_sha512_224(const void* data, usize len);
Mel_Sha512_256 mel_sha512_256(const void* data, usize len);

void       mel_sha224_init(Mel_Sha256_State* st);
void       mel_sha256_init(Mel_Sha256_State* st);
void       mel_sha256_update(Mel_Sha256_State* st, const void* data, usize len);
Mel_Sha224 mel_sha224_final(const Mel_Sha256_State* st);
Mel_Sha256 mel_sha256_final(const Mel_Sha256_State* st);

void           mel_sha384_init(Mel_Sha512_State* st);
void           mel_sha512_init(Mel_Sha512_State* st);
void           mel_sha512_224_init(Mel_Sha512_State* st);
void           mel_sha512_256_init(Mel_Sha512_State* st);
void           mel_sha512_update(Mel_Sha512_State* st, const void* data, usize len);
Mel_Sha384     mel_sha384_final(const Mel_Sha512_State* st);
Mel_Sha512     mel_sha512_final(const Mel_Sha512_State* st);
Mel_Sha512_224 mel_sha512_224_final(const Mel_Sha512_State* st);
Mel_Sha512_256 mel_sha512_256_final(const Mel_Sha512_State* st);
