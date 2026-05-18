#include "../reactor.internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

#define MEL_REACTOR_WIN32_POST (WM_APP + 0xBE17)

typedef struct Mel_Reactor_Backend {
    DWORD thread_id;
    bool  thread_id_set;
} Mel_Reactor_Backend;

bool mel__reactor_backend_adopt_system(Mel_Reactor_Internal* r)
{
    Mel_Reactor_Backend* b = (Mel_Reactor_Backend*)calloc(1, sizeof(*b));
    if (!b) return false;
    b->thread_id     = GetCurrentThreadId();
    b->thread_id_set = true;
    r->backend = b;

    MSG dummy;
    PeekMessageW(&dummy, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    return true;
}

void mel__reactor_backend_release_system(Mel_Reactor_Internal* r)
{
    if (!r->backend) return;
    free(r->backend);
    r->backend = NULL;
}

bool mel__reactor_backend_create_user(Mel_Reactor_Internal* r)
{
    (void)r;
    fprintf(stderr, "[reactor] user reactors are not yet implemented on win32\n");
    return false;
}

void mel__reactor_backend_destroy_user(Mel_Reactor_Internal* r)
{
    (void)r;
}

void mel__reactor_backend_run(Mel_Reactor_Internal* r)
{
    if (!r->backend || !r->backend->thread_id_set) return;
    if (GetCurrentThreadId() != r->backend->thread_id) {
        fprintf(stderr, "[reactor] run invoked from a thread that does not own this reactor\n");
        abort();
    }

    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            if (msg.hwnd == NULL && msg.message == MEL_REACTOR_WIN32_POST) {
                mel__reactor_dispatch_event(r, (void*)msg.lParam);
            } else {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        mel__reactor_dispatch_update(r);
        WaitMessage();
    }
}

void mel__reactor_backend_stop(Mel_Reactor_Internal* r)
{
    if (!r->backend || !r->backend->thread_id_set) return;
    PostThreadMessageW(r->backend->thread_id, WM_QUIT, 0, 0);
}

void mel__reactor_backend_post(Mel_Reactor_Internal* r, void* message)
{
    if (!r->backend || !r->backend->thread_id_set) return;
    PostThreadMessageW(r->backend->thread_id, MEL_REACTOR_WIN32_POST, 0, (LPARAM)message);
}

bool mel__reactor_backend_owns_caller(Mel_Reactor_Internal* r)
{
    if (!r->backend || !r->backend->thread_id_set) return false;
    return GetCurrentThreadId() == r->backend->thread_id;
}
