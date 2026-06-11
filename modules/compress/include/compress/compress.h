#pragma once

#include <compress/status.h>
#include <compress/codec.h>
#include <compress/registry.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u8*                 data;
    usize               len;
    Mel_Compress_Status status;
} Mel_Compress_Result;

typedef struct
{
    u32              level;
    const Mel_Alloc* alloc;
} Mel_Compress_Opt;

Mel_Compress_Result mel_compress_opt(const Mel_Compress_Codec* codec, str8 in, Mel_Compress_Opt opt);
#define mel_compress(codec, in, ...) mel_compress_opt((codec), (in), (Mel_Compress_Opt){ __VA_ARGS__ })

typedef struct
{
    const Mel_Alloc* alloc;
} Mel_Decompress_Opt;

Mel_Compress_Result mel_decompress_opt(const Mel_Compress_Codec* codec, str8 in, Mel_Decompress_Opt opt);
#define mel_decompress(codec, in, ...) mel_decompress_opt((codec), (in), (Mel_Decompress_Opt){ __VA_ARGS__ })

void mel_compress_result_free(Mel_Compress_Result* r, const Mel_Alloc* alloc);

#ifdef __cplusplus
}
#endif
