#include "nano.h"
#include <core/compiler.h>

#ifdef _WIN32

#include <Windows.h>

static LARGE_INTEGER Frequency = {0};

MEL_CONSTRUCTOR
static void mel_nano_win32_init(void)
{
    QueryPerformanceFrequency(&Frequency);
}

uint64_t mel_nanos_since_unspecified_epoch(void)
{
  
    LARGE_INTEGER Time;
    QueryPerformanceCounter(&Time);

    uint64_t Secs  = Time.QuadPart / Frequency.QuadPart;
    uint64_t Nanos = Time.QuadPart % Frequency.QuadPart * NOB_NANOS_PER_SEC / Frequency.QuadPart;
    return MEL_NANOS_PER_SEC * Secs + Nanos;
}

#endif

