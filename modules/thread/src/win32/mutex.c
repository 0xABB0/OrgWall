#include <thread/mutex.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct Mel__Win32_Mutex {
    SRWLOCK lock;
    DWORD   owner_tid;
    u32     depth_and_flags;
} Mel__Win32_Mutex;

static_assert(sizeof(Mel__Win32_Mutex) <= MEL_MUTEX_STORAGE_SIZE, "MEL_MUTEX_STORAGE_SIZE too small");

#define MEL__MUTEX(m)        ((Mel__Win32_Mutex*)(m)->_storage)
#define MEL__IS_RECURSIVE(w) ((w)->depth_and_flags & 1u)
#define MEL__DEPTH(w)        ((w)->depth_and_flags >> 1)
#define MEL__SET_DEPTH(w, d) ((w)->depth_and_flags = ((d) << 1) | MEL__IS_RECURSIVE(w))

bool mel_mutex_init(Mel_Mutex* m, Mel_Mutex_Kind kind)
{
    Mel__Win32_Mutex* w = MEL__MUTEX(m);
    InitializeSRWLock(&w->lock);
    w->owner_tid       = 0;
    w->depth_and_flags = (kind == MEL_MUTEX_RECURSIVE) ? 1u : 0u;
    return true;
}

void mel_mutex_destroy(Mel_Mutex* m)
{
    (void)m;
}

void mel_mutex_lock(Mel_Mutex* m)
{
    Mel__Win32_Mutex* w = MEL__MUTEX(m);
    if (!MEL__IS_RECURSIVE(w)) {
        AcquireSRWLockExclusive(&w->lock);
        return;
    }
    DWORD me = GetCurrentThreadId();
    if (w->owner_tid == me) {
        MEL__SET_DEPTH(w, MEL__DEPTH(w) + 1);
        return;
    }
    AcquireSRWLockExclusive(&w->lock);
    w->owner_tid = me;
    MEL__SET_DEPTH(w, 1);
}

bool mel_mutex_trylock(Mel_Mutex* m)
{
    Mel__Win32_Mutex* w = MEL__MUTEX(m);
    if (!MEL__IS_RECURSIVE(w)) {
        return TryAcquireSRWLockExclusive(&w->lock) != 0;
    }
    DWORD me = GetCurrentThreadId();
    if (w->owner_tid == me) {
        MEL__SET_DEPTH(w, MEL__DEPTH(w) + 1);
        return true;
    }
    if (!TryAcquireSRWLockExclusive(&w->lock)) return false;
    w->owner_tid = me;
    MEL__SET_DEPTH(w, 1);
    return true;
}

void mel_mutex_unlock(Mel_Mutex* m)
{
    Mel__Win32_Mutex* w = MEL__MUTEX(m);
    if (!MEL__IS_RECURSIVE(w)) {
        ReleaseSRWLockExclusive(&w->lock);
        return;
    }
    u32 d = MEL__DEPTH(w);
    if (d > 1) {
        MEL__SET_DEPTH(w, d - 1);
        return;
    }
    w->owner_tid = 0;
    MEL__SET_DEPTH(w, 0);
    ReleaseSRWLockExclusive(&w->lock);
}
