#include <time/nano.h>

#ifndef _WIN32

#include <time.h>

uint64_t mel_nanos_since_unspecified_epoch(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return MEL_NANOS_PER_SEC * ts.tv_sec + ts.tv_nsec;
}

mel_nanosec mel_wall_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    return MEL_NANOS_PER_SEC * (u64)ts.tv_sec + (u64)ts.tv_nsec;
}

#endif
