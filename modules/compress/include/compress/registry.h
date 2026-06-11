#pragma once

#include <compress/codec.h>

#ifdef __cplusplus
extern "C"
{
#endif

void mel_compress_registry_init(const Mel_Alloc* alloc);
void mel_compress_registry_shutdown(void);

void mel_compress_register(const Mel_Compress_Codec* codec);

usize                     mel_compress_count(void);
const Mel_Compress_Codec* mel_compress_at(usize index);
const Mel_Compress_Codec* mel_compress_find(str8 id);
const Mel_Compress_Codec* mel_compress_sniff(str8 head);
const Mel_Compress_Codec* mel_compress_for_ext(str8 ext);

#ifdef __cplusplus
}
#endif
