#include <thread/latch.h>
#include <thread/futex.h>

#include <stdatomic.h>

void mel_latch_init(Mel_Latch* l, u32 count)
{
    atomic_store_explicit(&l->_count, count, memory_order_relaxed);
}

void mel_latch_count_down(Mel_Latch* l)
{
    u32 prev = atomic_fetch_sub_explicit(&l->_count, 1, memory_order_acq_rel);
    if (prev == 1) {
        mel_futex_wake_all(&l->_count);
    }
}

void mel_latch_wait(Mel_Latch* l)
{
    for (;;) {
        u32 c = atomic_load_explicit(&l->_count, memory_order_acquire);
        if (c == 0) return;
        mel_futex_wait(&l->_count, c);
    }
}

bool mel_latch_wait_for(Mel_Latch* l, i64 timeout_ns)
{
    u32 c = atomic_load_explicit(&l->_count, memory_order_acquire);
    if (c == 0) return true;
    if (!mel_futex_wait_for(&l->_count, c, timeout_ns)) return false;
    return atomic_load_explicit(&l->_count, memory_order_acquire) == 0;
}

bool mel_latch_is_ready(const Mel_Latch* l)
{
    return atomic_load_explicit((_Atomic(u32)*)&l->_count, memory_order_acquire) == 0;
}
