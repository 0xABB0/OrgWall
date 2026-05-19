#pragma once

#include <stdatomic.h>

typedef struct spinlock spinlock;

struct spinlock
{
  _Atomic(bool) lock;
};

static inline void spinlock_lock(spinlock* l)
{
  while (atomic_exchange_explicit(&l->lock, true, memory_order_acquire)) { /* spin */ }
}

static inline void spinlock_unlock(spinlock* l)
{
  atomic_store_explicit(&l->lock, false, memory_order_release);
}
