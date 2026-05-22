#include <thread/sem.h>

#include <dispatch/dispatch.h>

static_assert(sizeof(dispatch_semaphore_t) <= MEL_SEM_STORAGE_SIZE, "MEL_SEM_STORAGE_SIZE too small");
static_assert(_Alignof(dispatch_semaphore_t) <= MEL_SEM_STORAGE_ALIGN, "MEL_SEM_STORAGE_ALIGN too small");

#define MEL__SEM(s) (*(dispatch_semaphore_t*)(s)->_storage)

bool mel_sem_init(Mel_Sem* s, u32 initial)
{
    MEL__SEM(s) = dispatch_semaphore_create((long)initial);
    return MEL__SEM(s) != NULL;
}

void mel_sem_destroy(Mel_Sem* s)
{
    MEL__SEM(s) = NULL;
}

void mel_sem_wait(Mel_Sem* s)
{
    dispatch_semaphore_wait(MEL__SEM(s), DISPATCH_TIME_FOREVER);
}

bool mel_sem_trywait(Mel_Sem* s)
{
    return dispatch_semaphore_wait(MEL__SEM(s), DISPATCH_TIME_NOW) == 0;
}

bool mel_sem_wait_for(Mel_Sem* s, i64 timeout_ns)
{
    dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, timeout_ns);
    return dispatch_semaphore_wait(MEL__SEM(s), when) == 0;
}

void mel_sem_post(Mel_Sem* s)
{
    dispatch_semaphore_signal(MEL__SEM(s));
}
