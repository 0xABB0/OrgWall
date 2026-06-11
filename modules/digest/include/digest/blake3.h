#pragma once

#include <core/types.h>

typedef struct Mel_Blake3_State
{
    u32   key[8];
    u32   cv_stack[54][8];
    usize cv_stack_len;
    u32   chunk_cv[8];
    u64   chunk_counter;
    u8    block[64];
    usize block_len;
    u32   blocks_compressed;
    u32   flags;
} Mel_Blake3_State;

void mel_blake3(const void* data, usize len, void* out, usize out_len);
void mel_blake3_keyed(const u8 key[32], const void* data, usize len, void* out, usize out_len);
void mel_blake3_derive_key(const void* context, usize context_len, const void* material, usize material_len, void* out, usize out_len);

void mel_blake3_init(Mel_Blake3_State* st);
void mel_blake3_init_keyed(Mel_Blake3_State* st, const u8 key[32]);
void mel_blake3_init_derive_key(Mel_Blake3_State* st, const void* context, usize context_len);
void mel_blake3_update(Mel_Blake3_State* st, const void* data, usize len);
void mel_blake3_final(const Mel_Blake3_State* st, void* out, usize out_len);
