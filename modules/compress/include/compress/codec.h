#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <string/str8.h>

#include <compress/status.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Compress_Stream Mel_Compress_Stream;

typedef struct
{
    bool             decompress;
    u32              level;
    const Mel_Alloc* alloc;
} Mel_Compress_Begin;

typedef struct
{
    usize               in_consumed;
    usize               out_produced;
    bool                finished;
    Mel_Compress_Status status;
} Mel_Compress_Step;

typedef struct Mel_Compress_Codec
{
    str8 id;
    str8 ext;
    u32  level_min;
    u32  level_max;
    u32  level_default;
    bool (*sniff)(str8 head);
    usize (*bound)(usize src_len, u32 level);
    Mel_Compress_Stream* (*begin)(Mel_Compress_Begin begin, Mel_Compress_Status* status);
    Mel_Compress_Step (*step)(Mel_Compress_Stream* s, str8 in, bool in_last, u8* out, usize out_cap);
    void (*end)(Mel_Compress_Stream* s);
} Mel_Compress_Codec;

#ifdef __cplusplus
}
#endif
