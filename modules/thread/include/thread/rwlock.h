#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <thread/storage.h>

typedef struct Mel_RWLock
{
    MEL_ALIGNAS(MEL_RWLOCK_STORAGE_ALIGN) byte _storage[MEL_RWLOCK_STORAGE_SIZE];
} Mel_RWLock;

bool mel_rwlock_init(Mel_RWLock* l);
void mel_rwlock_destroy(Mel_RWLock* l);
void mel_rwlock_lock_shared(Mel_RWLock* l);
bool mel_rwlock_trylock_shared(Mel_RWLock* l);
void mel_rwlock_unlock_shared(Mel_RWLock* l);
void mel_rwlock_lock(Mel_RWLock* l);
bool mel_rwlock_trylock(Mel_RWLock* l);
void mel_rwlock_unlock(Mel_RWLock* l);
