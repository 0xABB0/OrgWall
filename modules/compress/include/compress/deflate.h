#pragma once

#include <compress/codec.h>

#ifdef __cplusplus
extern "C"
{
#endif

const Mel_Compress_Codec* mel_compress_deflate(void);
const Mel_Compress_Codec* mel_compress_gzip(void);

#ifdef __cplusplus
}
#endif
