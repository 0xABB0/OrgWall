#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Process_Status;

#define MEL_PROCESS_SEVERITY_MASK 0x3u
#define MEL_PROCESS_OK            0u
#define MEL_PROCESS_WARNED        1u
#define MEL_PROCESS_ERROR         2u

#define MEL_PROCESS_CANCELLED     (1u << 2)
#define MEL_PROCESS_SPAWN_FAILED  (1u << 3)
#define MEL_PROCESS_NOT_FOUND     (1u << 4)
#define MEL_PROCESS_PERMISSION    (1u << 5)
#define MEL_PROCESS_BAD_HANDLE    (1u << 6)
#define MEL_PROCESS_UNAVAILABLE   (1u << 7)
#define MEL_PROCESS_EXITED        (1u << 8)
#define MEL_PROCESS_SIGNALLED     (1u << 9)
#define MEL_PROCESS_KILLED        (1u << 10)
#define MEL_PROCESS_DETACHED      (1u << 11)
#define MEL_PROCESS_PIPE_FAILED   (1u << 12)
#define MEL_PROCESS_NO_MEMORY     (1u << 13)
#define MEL_PROCESS_STILL_RUNNING (1u << 14)

static inline bool mel_process_status_failed(Mel_Process_Status s) { return (s & MEL_PROCESS_SEVERITY_MASK) == MEL_PROCESS_ERROR; }
static inline bool mel_process_status_warned(Mel_Process_Status s) { return (s & MEL_PROCESS_SEVERITY_MASK) == MEL_PROCESS_WARNED; }
static inline bool mel_process_status_ok(Mel_Process_Status s) { return (s & MEL_PROCESS_SEVERITY_MASK) == MEL_PROCESS_OK; }
static inline bool mel_process_status_cancelled(Mel_Process_Status s) { return (s & MEL_PROCESS_CANCELLED) != 0u; }
static inline bool mel_process_status_exited(Mel_Process_Status s) { return (s & MEL_PROCESS_EXITED) != 0u; }
static inline bool mel_process_status_signalled(Mel_Process_Status s) { return (s & MEL_PROCESS_SIGNALLED) != 0u; }

#ifdef __cplusplus
}
#endif
