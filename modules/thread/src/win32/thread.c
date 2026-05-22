#include <thread/thread.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <string.h>

static_assert(sizeof(HANDLE) <= MEL_THREAD_HANDLE_SIZE, "MEL_THREAD_HANDLE_SIZE too small");

#define MEL__THREAD_HANDLE(t) (*(HANDLE*)(t)->_handle)

static unsigned __stdcall mel__thread_trampoline(void* arg)
{
    Mel_Thread* t = (Mel_Thread*)arg;
    if (t->name[0]) mel_thread_set_name(t->name);
    int rc = t->fn(t->user);
    atomic_store_explicit(&t->finished, 1, memory_order_release);
    return (unsigned)rc;
}

bool mel_thread_spawn_opt(Mel_Thread* t, Mel_Thread_Fn fn, void* user, Mel_Thread_Spawn_Opt opt)
{
    t->fn   = fn;
    t->user = user;
    t->id   = 0;
    atomic_store_explicit(&t->finished, 0, memory_order_relaxed);

    if (opt.name) {
        usize n = strlen(opt.name);
        if (n > sizeof(t->name) - 1) n = sizeof(t->name) - 1;
        memcpy(t->name, opt.name, n);
        t->name[n] = 0;
    } else {
        t->name[0] = 0;
    }

    unsigned tid = 0;
    uintptr_t h = _beginthreadex(NULL, (unsigned)opt.stack_size, mel__thread_trampoline, t, 0, &tid);
    if (h == 0) return false;
    MEL__THREAD_HANDLE(t) = (HANDLE)h;
    t->id = (Mel_Thread_Id)tid;
    return true;
}

bool mel_thread_join(Mel_Thread* t, int* exit_code)
{
    if (WaitForSingleObject(MEL__THREAD_HANDLE(t), INFINITE) != WAIT_OBJECT_0) return false;
    if (exit_code) {
        DWORD code = 0;
        GetExitCodeThread(MEL__THREAD_HANDLE(t), &code);
        *exit_code = (int)code;
    }
    CloseHandle(MEL__THREAD_HANDLE(t));
    MEL__THREAD_HANDLE(t) = NULL;
    return true;
}

bool mel_thread_detach(Mel_Thread* t)
{
    BOOL ok = CloseHandle(MEL__THREAD_HANDLE(t));
    MEL__THREAD_HANDLE(t) = NULL;
    return ok != 0;
}

bool mel_thread_is_finished(const Mel_Thread* t)
{
    return atomic_load_explicit(&t->finished, memory_order_acquire) != 0;
}

Mel_Thread_Id mel_thread_current_id(void)
{
    return (Mel_Thread_Id)GetCurrentThreadId();
}

bool mel_thread_id_equal(Mel_Thread_Id a, Mel_Thread_Id b)
{
    return a == b;
}

void mel_thread_yield(void)
{
    SwitchToThread();
}

void mel_thread_sleep(i64 ns)
{
    if (ns <= 0) return;
    DWORD ms = (DWORD)(ns / 1000000);
    if (ms == 0) ms = 1;
    Sleep(ms);
}

void mel_thread_set_name(const char* name)
{
    wchar_t wname[16];
    int i = 0;
    for (; i < 15 && name[i]; i++) wname[i] = (wchar_t)(unsigned char)name[i];
    wname[i] = 0;
    SetThreadDescription(GetCurrentThread(), wname);
}

u32 mel_thread_hardware_concurrency(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (u32)info.dwNumberOfProcessors : 1;
}
