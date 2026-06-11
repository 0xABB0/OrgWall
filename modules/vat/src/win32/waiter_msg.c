#include <vat/vat.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <collection/list.h>
#include <debug/assert.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdatomic.h>

typedef struct
{
    Mel_Vat_Waiter   base;
    const Mel_Alloc* alloc;
    HANDLE           doorbell;
    DWORD            thread_id;
    atomic_bool      rung;
    Mel_Array(Mel_Vat_Wakeable*) armed;
} Msg_Waiter;

static bool msg_arm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    Msg_Waiter* w = mel_container_of(waiter, Msg_Waiter, base);
    mel_assert_msg("win32 waiter: arm off the opening thread", GetCurrentThreadId() == w->thread_id);
    for (usize i = 0; i < w->armed.count; i++)
    {
        if (w->armed.items[i] == wakeable)
            return true;
    }
    if (w->armed.count + 2 > MAXIMUM_WAIT_OBJECTS)
        return false;
    mel_array_push(&w->armed, wakeable);
    return true;
}

static void msg_disarm(Mel_Vat_Waiter* waiter, Mel_Vat_Wakeable* wakeable)
{
    Msg_Waiter* w = mel_container_of(waiter, Msg_Waiter, base);
    mel_assert_msg("win32 waiter: disarm off the opening thread", GetCurrentThreadId() == w->thread_id);
    for (usize i = 0; i < w->armed.count; i++)
    {
        if (w->armed.items[i] != wakeable)
            continue;
        mel_array_remove_unordered(&w->armed, i);
        return;
    }
}

static i32 msg_pump(void)
{
    i32 pumped = 0;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        pumped++;
    }
    return pumped;
}

static DWORD msg_timeout_ms(i64 timeout_ns)
{
    if (timeout_ns < 0)
        return INFINITE;
    i64 ms = (timeout_ns + 999999) / 1000000;
    if (ms >= (i64)INFINITE)
        return INFINITE - 1;
    return (DWORD)ms;
}

static i32 msg_wait(Mel_Vat_Waiter* waiter, i64 timeout_ns)
{
    Msg_Waiter* w = mel_container_of(waiter, Msg_Waiter, base);
    atomic_store_explicit(&w->rung, false, memory_order_seq_cst);

    i32 woke = msg_pump();

    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    handles[0] = w->doorbell;
    DWORD count = 1;
    for (usize i = 0; i < w->armed.count; i++)
        handles[count++] = (HANDLE)(uintptr_t)w->armed.items[i]->handle;

    DWORD timeout = woke > 0 ? 0 : msg_timeout_ms(timeout_ns);
    DWORD r = MsgWaitForMultipleObjectsEx(count, handles, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    mel_assert_msg("win32 waiter: MsgWaitForMultipleObjectsEx failed", r != WAIT_FAILED);

    if (r == WAIT_OBJECT_0 + count)
    {
        woke += msg_pump();
    }
    else if (r == WAIT_OBJECT_0)
    {
        woke++;
    }
    else if (r > WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + count)
    {
        Mel_Vat_Wakeable* wk = w->armed.items[r - WAIT_OBJECT_0 - 1];
        wk->revents |= wk->events;
        woke++;
    }
    return woke;
}

static void msg_ring(Mel_Vat_Waiter* waiter)
{
    Msg_Waiter* w = mel_container_of(waiter, Msg_Waiter, base);
    if (atomic_exchange_explicit(&w->rung, true, memory_order_seq_cst))
        return;
    SetEvent(w->doorbell);
}

static void msg_close(Mel_Vat_Waiter* waiter)
{
    Msg_Waiter* w = mel_container_of(waiter, Msg_Waiter, base);
    CloseHandle(w->doorbell);
    mel_array_free(&w->armed);
    mel_dealloc(w->alloc, w);
}

static const Mel_Vat_Waiter_Vtbl msg_vtbl = { msg_arm, msg_disarm, msg_wait, msg_ring, msg_close };

Mel_Vat_Waiter* mel_vat_waiter_ui(const Mel_Alloc* alloc)
{
    mel_assert(alloc != NULL);
    HANDLE doorbell = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (doorbell == NULL)
        return NULL;
    Msg_Waiter* w = mel_alloc_type(alloc, Msg_Waiter);
    w->base.vt = &msg_vtbl;
    w->alloc = alloc;
    w->doorbell = doorbell;
    w->thread_id = GetCurrentThreadId();
    atomic_init(&w->rung, false);
    mel_array_init(&w->armed, alloc);
    return &w->base;
}

Mel_Vat_Waiter* mel_vat_waiter_io(const Mel_Alloc* alloc) { return mel_vat_waiter_ui(alloc); }
