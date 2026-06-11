#pragma once

#include <compress/codec.h>

#ifdef __cplusplus
extern "C"
{
#endif

const Mel_Compress_Codec* mel_compress_zstd(void);

#ifdef __cplusplus
}
#endif
