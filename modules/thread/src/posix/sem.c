#include <thread/sem.h>

#include <semaphore.h>
#include <time.h>
#include <errno.h>

static_assert(sizeof(sem_t) <= MEL_SEM_STORAGE_SIZE, "MEL_SEM_STORAGE_SIZE too small");
static_assert(_Alignof(sem_t) <= MEL_SEM_STORAGE_ALIGN, "MEL_SEM_STORAGE_ALIGN too small");

#define MEL__SEM(s) ((sem_t*)(s)->_storage)

bool mel_sem_init(Mel_Sem* s, u32 initial)
{
    return sem_init(MEL__SEM(s), 0, initial) == 0;
}

void mel_sem_destroy(Mel_Sem* s)
{
    sem_destroy(MEL__SEM(s));
}

void mel_sem_wait(Mel_Sem* s)
{
    while (sem_wait(MEL__SEM(s)) != 0 && errno == EINTR) { }
}

bool mel_sem_trywait(Mel_Sem* s)
{
    while (sem_trywait(MEL__SEM(s)) != 0) {
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

bool mel_sem_wait_for(Mel_Sem* s, i64 timeout_ns)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    i64 sec  = timeout_ns / 1000000000;
    i64 nsec = timeout_ns % 1000000000;
    ts.tv_sec  += (time_t)sec;
    ts.tv_nsec += (long)nsec;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec  += 1;
        ts.tv_nsec -= 1000000000;
    }
    while (sem_timedwait(MEL__SEM(s), &ts) != 0) {
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

void mel_sem_post(Mel_Sem* s)
{
    sem_post(MEL__SEM(s));
}
