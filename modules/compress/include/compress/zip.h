#pragma once

#include <compress/compress.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Zip_Reader Mel_Zip_Reader;
typedef struct Mel_Zip_Writer Mel_Zip_Writer;

typedef struct
{
    str8 name;
    u64  size;
    u64  csize;
    u32  crc;
    bool dir;
} Mel_Zip_Entry;

Mel_Zip_Reader*     mel_zip_open(str8 bytes, const Mel_Alloc* alloc, Mel_Compress_Status* status);
usize               mel_zip_count(const Mel_Zip_Reader* r);
Mel_Zip_Entry       mel_zip_entry(const Mel_Zip_Reader* r, usize index);
Mel_Compress_Result mel_zip_extract(Mel_Zip_Reader* r, usize index);
void                mel_zip_close(Mel_Zip_Reader* r);

Mel_Zip_Writer*     mel_zip_writer_create(const Mel_Alloc* alloc, Mel_Compress_Status* status);
Mel_Compress_Status mel_zip_add(Mel_Zip_Writer* w, str8 name, str8 bytes, u32 level);
Mel_Compress_Result mel_zip_finish(Mel_Zip_Writer* w);

#ifdef __cplusplus
}
#endif
