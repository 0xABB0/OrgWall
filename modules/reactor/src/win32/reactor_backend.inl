#ifdef _CLANGD
#pragma once
#include "../reactor.c"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MEL_REACTOR_WIN32_MAX_HANDLES (MAXIMUM_WAIT_OBJECTS - 1)

static bool reactor_backend_init(Mel_Reactor* r)
{
    r->owner     = mel_thread_current_id();
    r->has_owner = true;
    MSG msg;
    PeekMessageW(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    return true;
}

static void reactor_backend_shutdown(Mel_Reactor* r)
{
    (void)r;
}

static void reactor_backend_wake(Mel_Reactor* r)
{
    PostThreadMessageW((DWORD)r->owner, WM_NULL, 0, 0);
}

static bool reactor_win32_drain(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

static bool reactor_backend_wait(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count, i32 timeout)
{
    for (usize i = 0; i < poll_count; i++) {
        if (polls[i]) polls[i]->revents = 0;
    }

    if (timeout == 0) {
        bool keep = reactor_win32_drain();
        for (usize i = 0; i < poll_count; i++) {
            if (polls[i] && WaitForSingleObject((HANDLE)(intptr_t)polls[i]->handle, 0) == WAIT_OBJECT_0) {
                polls[i]->revents |= MEL_REACTOR_POLL_IN;
            }
        }
        return keep;
    }

    HANDLE            handles[MEL_REACTOR_WIN32_MAX_HANDLES];
    Mel_Reactor_Poll* slots[MEL_REACTOR_WIN32_MAX_HANDLES];
    DWORD             nh = 0;
    for (usize i = 0; i < poll_count && nh < MEL_REACTOR_WIN32_MAX_HANDLES; i++) {
        if (!polls[i]) continue;
        handles[nh] = (HANDLE)(intptr_t)polls[i]->handle;
        slots[nh]   = polls[i];
        nh++;
    }

    if (nh == 0 && timeout < 0) {
        MSG  msg;
        BOOL got = GetMessageW(&msg, NULL, 0, 0);
        if (got == 0)  return false;
        if (got == -1) return true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        return reactor_win32_drain();
    }

    DWORD ms = timeout < 0 ? INFINITE : (DWORD)timeout;
    DWORD wr = MsgWaitForMultipleObjectsEx(nh, nh ? handles : NULL, ms,
                                           QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (wr < WAIT_OBJECT_0 + nh) {
        slots[wr - WAIT_OBJECT_0]->revents |= MEL_REACTOR_POLL_IN;
    }
    bool keep = reactor_win32_drain();
    for (DWORD i = 0; i < nh; i++) {
        if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) {
            slots[i]->revents |= MEL_REACTOR_POLL_IN;
        }
    }
    return keep;
}
