#include <thread/mutex.h>

#include <pthread.h>

static_assert(sizeof(pthread_mutex_t) <= MEL_MUTEX_STORAGE_SIZE, "MEL_MUTEX_STORAGE_SIZE too small");
static_assert(_Alignof(pthread_mutex_t) <= MEL_MUTEX_STORAGE_ALIGN, "MEL_MUTEX_STORAGE_ALIGN too small");

#define MEL__MUTEX(m) ((pthread_mutex_t*)(m)->_storage)

bool mel_mutex_init(Mel_Mutex* m, Mel_Mutex_Kind kind)
{
    if (kind == MEL_MUTEX_PLAIN) {
        return pthread_mutex_init(MEL__MUTEX(m), NULL) == 0;
    }
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) return false;
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int rc = pthread_mutex_init(MEL__MUTEX(m), &attr);
    pthread_mutexattr_destroy(&attr);
    return rc == 0;
}

void mel_mutex_destroy(Mel_Mutex* m)
{
    pthread_mutex_destroy(MEL__MUTEX(m));
}

void mel_mutex_lock(Mel_Mutex* m)
{
    pthread_mutex_lock(MEL__MUTEX(m));
}

bool mel_mutex_trylock(Mel_Mutex* m)
{
    return pthread_mutex_trylock(MEL__MUTEX(m)) == 0;
}

void mel_mutex_unlock(Mel_Mutex* m)
{
    pthread_mutex_unlock(MEL__MUTEX(m));
}
