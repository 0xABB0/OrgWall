#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Compress_Status;

#define MEL_COMPRESS_SEVERITY_MASK  0x3u
#define MEL_COMPRESS_OK             0u
#define MEL_COMPRESS_WARNED         1u
#define MEL_COMPRESS_ERROR          2u

#define MEL_COMPRESS_CORRUPT        (1u << 2)
#define MEL_COMPRESS_TRUNCATED      (1u << 3)
#define MEL_COMPRESS_NO_MEMORY      (1u << 4)
#define MEL_COMPRESS_BAD_LEVEL      (1u << 5)
#define MEL_COMPRESS_UNKNOWN_FORMAT (1u << 6)
#define MEL_COMPRESS_OUTPUT_FULL    (1u << 7)
#define MEL_COMPRESS_BAD_STATE      (1u << 8)

static inline bool mel_compress_status_ok(Mel_Compress_Status s) { return (s & MEL_COMPRESS_SEVERITY_MASK) == MEL_COMPRESS_OK; }
static inline bool mel_compress_status_warned(Mel_Compress_Status s) { return (s & MEL_COMPRESS_SEVERITY_MASK) == MEL_COMPRESS_WARNED; }
static inline bool mel_compress_status_failed(Mel_Compress_Status s) { return (s & MEL_COMPRESS_SEVERITY_MASK) == MEL_COMPRESS_ERROR; }

#ifdef __cplusplus
}
#endif
