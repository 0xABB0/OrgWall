#pragma once

#include <core/types.h>
#include <platform/platform.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_PLATFORM_SCHED_OTHER = 0u,
    MEL_PLATFORM_SCHED_BATCH = 1u << 0,
    MEL_PLATFORM_SCHED_IDLE = 1u << 1,
    MEL_PLATFORM_SCHED_FIFO = 1u << 2,
    MEL_PLATFORM_SCHED_RR = 1u << 3,
};

typedef struct
{
    u32 policy;
    i32 priority;
    i32 nice;
} Mel_Platform_Thread_Sched;

Mel_Platform_Status mel_platform_linux_set_thread_sched(Mel_Platform_Thread_Sched sched);
Mel_Platform_Status mel_platform_linux_get_thread_sched(Mel_Platform_Thread_Sched* out);

#ifdef __cplusplus
}
#endif
