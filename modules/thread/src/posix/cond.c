#include <thread/cond.h>

#include <pthread.h>
#include <time.h>
#include <errno.h>

static_assert(sizeof(pthread_cond_t) <= MEL_COND_STORAGE_SIZE, "MEL_COND_STORAGE_SIZE too small");
static_assert(_Alignof(pthread_cond_t) <= MEL_COND_STORAGE_ALIGN, "MEL_COND_STORAGE_ALIGN too small");

#define MEL__COND(c)  ((pthread_cond_t*)(c)->_storage)
#define MEL__MUTEX(m) ((pthread_mutex_t*)(m)->_storage)

bool mel_cond_init(Mel_Cond* c)
{
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) return false;
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    int rc = pthread_cond_init(MEL__COND(c), &attr);
    pthread_condattr_destroy(&attr);
    return rc == 0;
}

void mel_cond_destroy(Mel_Cond* c)
{
    pthread_cond_destroy(MEL__COND(c));
}

void mel_cond_wait(Mel_Cond* c, Mel_Mutex* m)
{
    pthread_cond_wait(MEL__COND(c), MEL__MUTEX(m));
}

bool mel_cond_wait_for(Mel_Cond* c, Mel_Mutex* m, i64 timeout_ns)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    i64 sec  = timeout_ns / 1000000000;
    i64 nsec = timeout_ns % 1000000000;
    ts.tv_sec  += (time_t)sec;
    ts.tv_nsec += (long)nsec;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec  += 1;
        ts.tv_nsec -= 1000000000;
    }
    return pthread_cond_timedwait(MEL__COND(c), MEL__MUTEX(m), &ts) == 0;
}

void mel_cond_signal(Mel_Cond* c)
{
    pthread_cond_signal(MEL__COND(c));
}

void mel_cond_broadcast(Mel_Cond* c)
{
    pthread_cond_broadcast(MEL__COND(c));
}
