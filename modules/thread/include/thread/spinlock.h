#pragma once

#include <core/types.h>

#include <stdatomic.h>

typedef struct Mel_Spinlock {
    _Atomic(bool) _state;
} Mel_Spinlock;

static inline void mel_spinlock_lock(Mel_Spinlock* l)
{
    while (atomic_exchange_explicit(&l->_state, true, memory_order_acquire)) { }
}

static inline bool mel_spinlock_trylock(Mel_Spinlock* l)
{
    return !atomic_exchange_explicit(&l->_state, true, memory_order_acquire);
}

static inline void mel_spinlock_unlock(Mel_Spinlock* l)
{
    atomic_store_explicit(&l->_state, false, memory_order_release);
}
