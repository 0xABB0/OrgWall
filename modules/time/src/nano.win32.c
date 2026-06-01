#include <time/nano.h>
#include <core/compiler.h>

#ifdef _WIN32

#include <Windows.h>

static LARGE_INTEGER Frequency = { 0 };

MEL_CONSTRUCTOR
static void mel_nano_win32_init(void) { QueryPerformanceFrequency(&Frequency); }

uint64_t mel_nanos_since_unspecified_epoch(void)
{
    LARGE_INTEGER Time;
    QueryPerformanceCounter(&Time);

    uint64_t Secs = Time.QuadPart / Frequency.QuadPart;
    uint64_t Nanos = Time.QuadPart % Frequency.QuadPart * MEL_NANOS_PER_SEC / Frequency.QuadPart;
    return MEL_NANOS_PER_SEC * Secs + Nanos;
}

mel_nanosec mel_wall_now_ns(void)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);

    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;

    uint64_t unix100 = u.QuadPart - 116444736000000000ULL;
    return unix100 * 100;
}

#endif
