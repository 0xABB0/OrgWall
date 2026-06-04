#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Storage_Status;

#define MEL_STORAGE_SEVERITY_MASK 0x3u
#define MEL_STORAGE_OK            0u
#define MEL_STORAGE_WARNED        1u
#define MEL_STORAGE_ERROR         2u

#define MEL_STORAGE_CANCELLED       (1u << 2)
#define MEL_STORAGE_NOT_FOUND       (1u << 3)
#define MEL_STORAGE_EXISTS          (1u << 4)
#define MEL_STORAGE_PERMISSION      (1u << 5)
#define MEL_STORAGE_NOT_A_DIRECTORY (1u << 6)
#define MEL_STORAGE_IS_A_DIRECTORY  (1u << 7)
#define MEL_STORAGE_NOT_EMPTY       (1u << 8)
#define MEL_STORAGE_NO_SPACE        (1u << 9)
#define MEL_STORAGE_NAME_TOO_LONG   (1u << 10)
#define MEL_STORAGE_PARTIAL         (1u << 11)
#define MEL_STORAGE_CROSS_DEVICE    (1u << 12)
#define MEL_STORAGE_READ_ONLY       (1u << 13)
#define MEL_STORAGE_UNAVAILABLE     (1u << 14)
#define MEL_STORAGE_BAD_PATH        (1u << 15)
#define MEL_STORAGE_ESCAPE          (1u << 16)
#define MEL_STORAGE_SIZE_MISMATCH   (1u << 17)
#define MEL_STORAGE_NOT_READY       (1u << 18)

static inline bool mel_storage_ok(Mel_Storage_Status s) { return (s & MEL_STORAGE_SEVERITY_MASK) == MEL_STORAGE_OK; }
static inline bool mel_storage_warned(Mel_Storage_Status s) { return (s & MEL_STORAGE_SEVERITY_MASK) == MEL_STORAGE_WARNED; }
static inline bool mel_storage_failed(Mel_Storage_Status s) { return (s & MEL_STORAGE_SEVERITY_MASK) == MEL_STORAGE_ERROR; }
static inline bool mel_storage_cancelled(Mel_Storage_Status s) { return (s & MEL_STORAGE_CANCELLED) != 0u; }
static inline bool mel_storage_not_found(Mel_Storage_Status s) { return (s & MEL_STORAGE_NOT_FOUND) != 0u; }
static inline bool mel_storage_read_only(Mel_Storage_Status s) { return (s & MEL_STORAGE_READ_ONLY) != 0u; }
static inline bool mel_storage_escape(Mel_Storage_Status s) { return (s & MEL_STORAGE_ESCAPE) != 0u; }
static inline bool mel_storage_partial(Mel_Storage_Status s) { return (s & MEL_STORAGE_PARTIAL) != 0u; }

#ifdef __cplusplus
}
#endif
