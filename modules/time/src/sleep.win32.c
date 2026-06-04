#include "sleep_backend.h"

#ifdef _WIN32

#include <time/duration.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void mel__sleep_block(u64 ns)
{
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer)
    {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ns / 100);
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
            WaitForSingleObject(timer, INFINITE);
        CloseHandle(timer);
        return;
    }

    DWORD ms = (DWORD)(ns / (u64)MEL_NANOS_PER_MS);
    if (ms == 0 && ns != 0)
        ms = 1;
    Sleep(ms);
}

#endif
