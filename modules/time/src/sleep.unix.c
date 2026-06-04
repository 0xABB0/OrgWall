#include "sleep_backend.h"

#ifndef _WIN32

#include <time/duration.h>

#include <errno.h>
#include <time.h>

void mel__sleep_block(u64 ns)
{
    struct timespec req = {
        .tv_sec = (time_t)(ns / (u64)MEL_NANOS_PER_SEC),
        .tv_nsec = (long)(ns % (u64)MEL_NANOS_PER_SEC),
    };
    struct timespec rem;
    while (nanosleep(&req, &rem) != 0 && errno == EINTR)
        req = rem;
}

#endif
