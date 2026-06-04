#include <core/platform.h>

#if !MEL_PLATFORM_LINUX
#error "linux-only translation unit"
#endif

#include <platform/linux/sched.h>

#include <sched.h>
#include <sys/resource.h>
#include <errno.h>

static int policy_to_native(u32 policy)
{
    switch (policy)
    {
    case MEL_PLATFORM_SCHED_BATCH:
        return SCHED_BATCH;
    case MEL_PLATFORM_SCHED_IDLE:
        return SCHED_IDLE;
    case MEL_PLATFORM_SCHED_FIFO:
        return SCHED_FIFO;
    case MEL_PLATFORM_SCHED_RR:
        return SCHED_RR;
    default:
        return SCHED_OTHER;
    }
}

static u32 policy_from_native(int policy)
{
    switch (policy)
    {
    case SCHED_BATCH:
        return MEL_PLATFORM_SCHED_BATCH;
    case SCHED_IDLE:
        return MEL_PLATFORM_SCHED_IDLE;
    case SCHED_FIFO:
        return MEL_PLATFORM_SCHED_FIFO;
    case SCHED_RR:
        return MEL_PLATFORM_SCHED_RR;
    default:
        return MEL_PLATFORM_SCHED_OTHER;
    }
}

Mel_Platform_Status mel_platform_linux_set_thread_sched(Mel_Platform_Thread_Sched sched)
{
    int                native = policy_to_native(sched.policy);
    struct sched_param param = { .sched_priority = sched.priority };
    if (sched_setscheduler(0, native, &param) != 0)
        return (errno == EPERM) ? (MEL_PLATFORM_ERROR | MEL_PLATFORM_DENIED) : (MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID);
    if (native == SCHED_OTHER || native == SCHED_BATCH)
    {
        errno = 0;
        if (setpriority(PRIO_PROCESS, 0, sched.nice) != 0 && errno != 0)
            return (errno == EPERM) ? (MEL_PLATFORM_ERROR | MEL_PLATFORM_DENIED) : (MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID);
    }
    return MEL_PLATFORM_OK;
}

Mel_Platform_Status mel_platform_linux_get_thread_sched(Mel_Platform_Thread_Sched* out)
{
    if (out == NULL)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID;
    int native = sched_getscheduler(0);
    if (native < 0)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID;
    struct sched_param param = { 0 };
    if (sched_getparam(0, &param) != 0)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID;
    errno = 0;
    int nice = getpriority(PRIO_PROCESS, 0);
    out->policy = policy_from_native(native);
    out->priority = param.sched_priority;
    out->nice = (nice == -1 && errno != 0) ? 0 : nice;
    return MEL_PLATFORM_OK;
}
