#include <thread/sem.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static_assert(sizeof(HANDLE) <= MEL_SEM_STORAGE_SIZE, "MEL_SEM_STORAGE_SIZE too small");

#define MEL__SEM(s) (*(HANDLE*)(s)->_storage)

bool mel_sem_init(Mel_Sem* s, u32 initial)
{
    MEL__SEM(s) = CreateSemaphoreA(NULL, (LONG)initial, 0x7FFFFFFF, NULL);
    return MEL__SEM(s) != NULL;
}

void mel_sem_destroy(Mel_Sem* s)
{
    CloseHandle(MEL__SEM(s));
    MEL__SEM(s) = NULL;
}

void mel_sem_wait(Mel_Sem* s)
{
    WaitForSingleObject(MEL__SEM(s), INFINITE);
}

bool mel_sem_trywait(Mel_Sem* s)
{
    return WaitForSingleObject(MEL__SEM(s), 0) == WAIT_OBJECT_0;
}

bool mel_sem_wait_for(Mel_Sem* s, i64 timeout_ns)
{
    DWORD ms = (DWORD)(timeout_ns / 1000000);
    return WaitForSingleObject(MEL__SEM(s), ms) == WAIT_OBJECT_0;
}

void mel_sem_post(Mel_Sem* s)
{
    ReleaseSemaphore(MEL__SEM(s), 1, NULL);
}
