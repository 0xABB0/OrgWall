#include <thread/futex.h>

#include <os/os_sync_wait_on_address.h>

void mel_futex_wait(_Atomic(u32)* addr, u32 expected)
{
    os_sync_wait_on_address((void*)addr, (u64)expected, sizeof(u32), OS_SYNC_WAIT_ON_ADDRESS_NONE);
}

bool mel_futex_wait_for(_Atomic(u32)* addr, u32 expected, i64 timeout_ns)
{
    int rc = os_sync_wait_on_address_with_timeout(
        (void*)addr, (u64)expected, sizeof(u32),
        OS_SYNC_WAIT_ON_ADDRESS_NONE,
        OS_CLOCK_MACH_ABSOLUTE_TIME, (u64)timeout_ns);
    return rc >= 0;
}

void mel_futex_wake_one(_Atomic(u32)* addr)
{
    os_sync_wake_by_address_any((void*)addr, sizeof(u32), OS_SYNC_WAKE_BY_ADDRESS_NONE);
}

void mel_futex_wake_all(_Atomic(u32)* addr)
{
    os_sync_wake_by_address_all((void*)addr, sizeof(u32), OS_SYNC_WAKE_BY_ADDRESS_NONE);
}
