#include <thread/barrier.h>
#include <thread/futex.h>

#include <stdatomic.h>

typedef struct Mel__Barrier_Body {
    _Atomic(u32) generation;
    _Atomic(u32) arrived;
    u32          total;
} Mel__Barrier_Body;

static_assert(sizeof(Mel__Barrier_Body) <= MEL_BARRIER_STORAGE_SIZE, "MEL_BARRIER_STORAGE_SIZE too small");

#define MEL__BARRIER(b) ((Mel__Barrier_Body*)(b)->_storage)

bool mel_barrier_init(Mel_Barrier* b, u32 count)
{
    Mel__Barrier_Body* body = MEL__BARRIER(b);
    atomic_store_explicit(&body->generation, 0, memory_order_relaxed);
    atomic_store_explicit(&body->arrived, 0, memory_order_relaxed);
    body->total = count;
    return true;
}

void mel_barrier_destroy(Mel_Barrier* b)
{
    (void)b;
}

bool mel_barrier_wait(Mel_Barrier* b)
{
    Mel__Barrier_Body* body = MEL__BARRIER(b);
    u32 g = atomic_load_explicit(&body->generation, memory_order_relaxed);
    u32 a = atomic_fetch_add_explicit(&body->arrived, 1, memory_order_acq_rel) + 1;
    if (a == body->total) {
        atomic_store_explicit(&body->arrived, 0, memory_order_relaxed);
        atomic_fetch_add_explicit(&body->generation, 1, memory_order_release);
        mel_futex_wake_all(&body->generation);
        return true;
    }
    while (atomic_load_explicit(&body->generation, memory_order_acquire) == g) {
        mel_futex_wait(&body->generation, g);
    }
    return false;
}
