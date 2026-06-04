#pragma once

#include <core/types.h>

#include <allocator/allocator.fwd.h>

#include <collection/array.fwd.h>

#include <image/image.h>

typedef void (*Mel_Image_Write_Fn)(void* user, const void* bytes, usize len);

typedef struct
{
    const char* name;
    bool (*probe)(const u8* bytes, usize len);
    bool (*decode)(const u8* bytes, usize len, const Mel_Alloc* a, Mel_Image* out);
    bool (*encode)(const Mel_Image* img, Mel_Image_Write_Fn write_fn, void* user, const Mel_Alloc* a);
} Mel_Image_Codec_Desc;

void mel_image_codec_init(const Mel_Alloc* a);
void mel_image_codec_shutdown(void);
void mel_image_codec_register(const Mel_Image_Codec_Desc* codec);

const Mel_Image_Codec_Desc* mel_image_codec_find(const char* name);
const Mel_Image_Codec_Desc* mel_image_codec_probe(const u8* bytes, usize len);

bool mel_image_load(Mel_Image* out, const u8* bytes, usize len, const Mel_Alloc* a);
bool mel_image_load_file(Mel_Image* out, const char* path, const Mel_Alloc* a);

bool mel_image_save(const Mel_Image* img, const char* path, const Mel_Alloc* a);
bool mel_image_save_as(const Mel_Image* img, const char* path, const char* codec_name, const Mel_Alloc* a);

typedef Mel_Array(u8) Mel_Image_Bytes;

bool mel_image_encode(const Mel_Image* img, const char* codec_name, Mel_Image_Bytes* out, const Mel_Alloc* a);
