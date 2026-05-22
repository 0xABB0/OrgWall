#include <thread/futex.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma comment(lib, "Synchronization.lib")

void mel_futex_wait(_Atomic(u32)* addr, u32 expected)
{
    u32 cmp = expected;
    WaitOnAddress((volatile VOID*)addr, &cmp, sizeof(u32), INFINITE);
}

bool mel_futex_wait_for(_Atomic(u32)* addr, u32 expected, i64 timeout_ns)
{
    u32 cmp = expected;
    DWORD ms = (DWORD)(timeout_ns / 1000000);
    return WaitOnAddress((volatile VOID*)addr, &cmp, sizeof(u32), ms) != 0;
}

void mel_futex_wake_one(_Atomic(u32)* addr)
{
    WakeByAddressSingle((PVOID)addr);
}

void mel_futex_wake_all(_Atomic(u32)* addr)
{
    WakeByAddressAll((PVOID)addr);
}
