#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_IO_Status;

#define MEL_IO_SEVERITY_MASK 0x3u
#define MEL_IO_OK            0u
#define MEL_IO_WARNED        1u
#define MEL_IO_ERROR         2u

#define MEL_IO_CANCELLED     (1u << 2)
#define MEL_IO_EOF           (1u << 3)
#define MEL_IO_PARTIAL       (1u << 4)
#define MEL_IO_PEER_CLOSE    (1u << 5)
#define MEL_IO_BAD_HANDLE    (1u << 6)
#define MEL_IO_UNAVAILABLE   (1u << 7)
#define MEL_IO_NOT_FOUND     (1u << 8)
#define MEL_IO_PERMISSION    (1u << 9)
#define MEL_IO_EXISTS        (1u << 10)
#define MEL_IO_NOT_SEEKABLE  (1u << 11)
#define MEL_IO_READ_ONLY     (1u << 12)
#define MEL_IO_WRITE_ONLY    (1u << 13)
#define MEL_IO_NO_SPACE      (1u << 14)
#define MEL_IO_TRUNCATED     (1u << 15)

static inline bool mel_io_status_failed(Mel_IO_Status s) { return (s & MEL_IO_SEVERITY_MASK) == MEL_IO_ERROR; }
static inline bool mel_io_status_warned(Mel_IO_Status s) { return (s & MEL_IO_SEVERITY_MASK) == MEL_IO_WARNED; }
static inline bool mel_io_status_ok(Mel_IO_Status s) { return (s & MEL_IO_SEVERITY_MASK) == MEL_IO_OK; }
static inline bool mel_io_status_cancelled(Mel_IO_Status s) { return (s & MEL_IO_CANCELLED) != 0u; }
static inline bool mel_io_status_eof(Mel_IO_Status s) { return (s & MEL_IO_EOF) != 0u; }
static inline bool mel_io_status_partial(Mel_IO_Status s) { return (s & MEL_IO_PARTIAL) != 0u; }

#ifdef __cplusplus
}
#endif
