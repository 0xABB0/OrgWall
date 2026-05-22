#include <thread/rwlock.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static_assert(sizeof(SRWLOCK) <= MEL_RWLOCK_STORAGE_SIZE, "MEL_RWLOCK_STORAGE_SIZE too small");

#define MEL__RWLOCK(l) ((SRWLOCK*)(l)->_storage)

bool mel_rwlock_init(Mel_RWLock* l)
{
    InitializeSRWLock(MEL__RWLOCK(l));
    return true;
}

void mel_rwlock_destroy(Mel_RWLock* l)
{
    (void)l;
}

void mel_rwlock_lock_shared(Mel_RWLock* l)
{
    AcquireSRWLockShared(MEL__RWLOCK(l));
}

bool mel_rwlock_trylock_shared(Mel_RWLock* l)
{
    return TryAcquireSRWLockShared(MEL__RWLOCK(l)) != 0;
}

void mel_rwlock_unlock_shared(Mel_RWLock* l)
{
    ReleaseSRWLockShared(MEL__RWLOCK(l));
}

void mel_rwlock_lock(Mel_RWLock* l)
{
    AcquireSRWLockExclusive(MEL__RWLOCK(l));
}

bool mel_rwlock_trylock(Mel_RWLock* l)
{
    return TryAcquireSRWLockExclusive(MEL__RWLOCK(l)) != 0;
}

void mel_rwlock_unlock(Mel_RWLock* l)
{
    ReleaseSRWLockExclusive(MEL__RWLOCK(l));
}
