#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Dylib_Status;

#define MEL_DYLIB_SEVERITY_MASK 0x3u
#define MEL_DYLIB_OK            0u
#define MEL_DYLIB_WARNED        1u
#define MEL_DYLIB_ERROR         2u

#define MEL_DYLIB_NOT_FOUND     (1u << 2)
#define MEL_DYLIB_NO_SYMBOL     (1u << 3)
#define MEL_DYLIB_PERMISSION    (1u << 4)
#define MEL_DYLIB_BAD_IMAGE     (1u << 5)
#define MEL_DYLIB_INIT_FAILED   (1u << 6)
#define MEL_DYLIB_UNAVAILABLE   (1u << 7)
#define MEL_DYLIB_BAD_HANDLE    (1u << 8)
#define MEL_DYLIB_OUT_OF_MEMORY (1u << 9)

static inline bool mel_dylib_status_ok(Mel_Dylib_Status s) { return (s & MEL_DYLIB_SEVERITY_MASK) == MEL_DYLIB_OK; }
static inline bool mel_dylib_status_warned(Mel_Dylib_Status s) { return (s & MEL_DYLIB_SEVERITY_MASK) == MEL_DYLIB_WARNED; }
static inline bool mel_dylib_status_failed(Mel_Dylib_Status s) { return (s & MEL_DYLIB_SEVERITY_MASK) == MEL_DYLIB_ERROR; }
static inline bool mel_dylib_status_not_found(Mel_Dylib_Status s) { return (s & MEL_DYLIB_NOT_FOUND) != 0u; }
static inline bool mel_dylib_status_no_symbol(Mel_Dylib_Status s) { return (s & MEL_DYLIB_NO_SYMBOL) != 0u; }
static inline bool mel_dylib_status_unavailable(Mel_Dylib_Status s) { return (s & MEL_DYLIB_UNAVAILABLE) != 0u; }

#ifdef __cplusplus
}
#endif
