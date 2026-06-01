#pragma once

#include <allocator/allocator.h>
#include <core/types.h>

typedef struct mel_gf mel_gf;

struct mel_gf
{
    u16 order;
    u16 (*add)(const mel_gf*, u16, u16);
    u16 (*sub)(const mel_gf*, u16, u16);
    u16 (*mul)(const mel_gf*, u16, u16);
    u16*             exp;
    u16*             log;
    const Mel_Alloc* allocator;
};

bool mel_gf_binary_init(mel_gf* f, u16 order, u16 primitive_poly, const Mel_Alloc* allocator);
bool mel_gf_prime_init(mel_gf* f, u16 order, const Mel_Alloc* allocator);
void mel_gf_free(mel_gf* f);

u16 mel_gf_add(const mel_gf* f, u16 a, u16 b);
u16 mel_gf_sub(const mel_gf* f, u16 a, u16 b);
u16 mel_gf_mul(const mel_gf* f, u16 a, u16 b);
u16 mel_gf_pow(const mel_gf* f, u16 base, u32 exp);
u16 mel_gf_inv(const mel_gf* f, u16 a);
