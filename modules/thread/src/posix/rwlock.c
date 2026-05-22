#include <thread/rwlock.h>

#include <pthread.h>

static_assert(sizeof(pthread_rwlock_t) <= MEL_RWLOCK_STORAGE_SIZE, "MEL_RWLOCK_STORAGE_SIZE too small");
static_assert(_Alignof(pthread_rwlock_t) <= MEL_RWLOCK_STORAGE_ALIGN, "MEL_RWLOCK_STORAGE_ALIGN too small");

#define MEL__RWLOCK(l) ((pthread_rwlock_t*)(l)->_storage)

bool mel_rwlock_init(Mel_RWLock* l)
{
    return pthread_rwlock_init(MEL__RWLOCK(l), NULL) == 0;
}

void mel_rwlock_destroy(Mel_RWLock* l)
{
    pthread_rwlock_destroy(MEL__RWLOCK(l));
}

void mel_rwlock_lock_shared(Mel_RWLock* l)
{
    pthread_rwlock_rdlock(MEL__RWLOCK(l));
}

bool mel_rwlock_trylock_shared(Mel_RWLock* l)
{
    return pthread_rwlock_tryrdlock(MEL__RWLOCK(l)) == 0;
}

void mel_rwlock_unlock_shared(Mel_RWLock* l)
{
    pthread_rwlock_unlock(MEL__RWLOCK(l));
}

void mel_rwlock_lock(Mel_RWLock* l)
{
    pthread_rwlock_wrlock(MEL__RWLOCK(l));
}

bool mel_rwlock_trylock(Mel_RWLock* l)
{
    return pthread_rwlock_trywrlock(MEL__RWLOCK(l)) == 0;
}

void mel_rwlock_unlock(Mel_RWLock* l)
{
    pthread_rwlock_unlock(MEL__RWLOCK(l));
}
