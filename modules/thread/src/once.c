#include <thread/once.h>
#include <thread/futex.h>

#include <stdatomic.h>

enum { MEL__ONCE_IDLE = 0, MEL__ONCE_RUNNING = 1, MEL__ONCE_DONE = 2 };

#define MEL__ONCE_STATE(o) ((_Atomic(u32)*)(o)->_storage)

void mel_once(Mel_Once* o, Mel_Once_Fn fn)
{
    _Atomic(u32)* state = MEL__ONCE_STATE(o);
    u32 s = atomic_load_explicit(state, memory_order_acquire);
    if (s == MEL__ONCE_DONE) return;

    u32 expected = MEL__ONCE_IDLE;
    if (atomic_compare_exchange_strong_explicit(
            state, &expected, MEL__ONCE_RUNNING,
            memory_order_acquire, memory_order_acquire)) {
        fn();
        atomic_store_explicit(state, MEL__ONCE_DONE, memory_order_release);
        mel_futex_wake_all(state);
        return;
    }
    while (atomic_load_explicit(state, memory_order_acquire) != MEL__ONCE_DONE) {
        mel_futex_wait(state, MEL__ONCE_RUNNING);
    }
}
