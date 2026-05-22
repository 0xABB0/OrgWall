#include <thread/cond.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static_assert(sizeof(CONDITION_VARIABLE) <= MEL_COND_STORAGE_SIZE, "MEL_COND_STORAGE_SIZE too small");

#define MEL__COND(c) ((CONDITION_VARIABLE*)(c)->_storage)

typedef struct Mel__Win32_Mutex_Layout {
    SRWLOCK lock;
} Mel__Win32_Mutex_Layout;

bool mel_cond_init(Mel_Cond* c)
{
    InitializeConditionVariable(MEL__COND(c));
    return true;
}

void mel_cond_destroy(Mel_Cond* c)
{
    (void)c;
}

void mel_cond_wait(Mel_Cond* c, Mel_Mutex* m)
{
    SleepConditionVariableSRW(MEL__COND(c), (SRWLOCK*)m->_storage, INFINITE, 0);
}

bool mel_cond_wait_for(Mel_Cond* c, Mel_Mutex* m, i64 timeout_ns)
{
    DWORD ms = (DWORD)(timeout_ns / 1000000);
    return SleepConditionVariableSRW(MEL__COND(c), (SRWLOCK*)m->_storage, ms, 0) != 0;
}

void mel_cond_signal(Mel_Cond* c)
{
    WakeConditionVariable(MEL__COND(c));
}

void mel_cond_broadcast(Mel_Cond* c)
{
    WakeAllConditionVariable(MEL__COND(c));
}
