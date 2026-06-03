#include <thread/event.h>
#include <thread/futex.h>

#include <stdatomic.h>

enum
{
    MEL__EVENT_UNSIGNALED = 0,
    MEL__EVENT_SIGNALED = 1
};

typedef struct Mel__Thread_Event_Body
{
    _Atomic(u32) state;
    u32          manual_reset;
} Mel__Thread_Event_Body;

static_assert(sizeof(Mel__Thread_Event_Body) <= MEL_THREAD_EVENT_STORAGE_SIZE, "MEL_THREAD_EVENT_STORAGE_SIZE too small");

#define MEL__EVENT(e) ((Mel__Thread_Event_Body*)(e)->_storage)

bool mel_thread_event_init(Mel_Thread_Event* e, Mel_Thread_Event_Kind kind)
{
    Mel__Thread_Event_Body* body = MEL__EVENT(e);
    atomic_store_explicit(&body->state, MEL__EVENT_UNSIGNALED, memory_order_relaxed);
    body->manual_reset = (kind == MEL_THREAD_EVENT_MANUAL_RESET);
    return true;
}

void mel_thread_event_destroy(Mel_Thread_Event* e) { (void)e; }

void mel_thread_event_wait(Mel_Thread_Event* e)
{
    Mel__Thread_Event_Body* body = MEL__EVENT(e);
    for (;;)
    {
        if (body->manual_reset)
        {
            if (atomic_load_explicit(&body->state, memory_order_acquire) == MEL__EVENT_SIGNALED)
                return;
        }
        else
        {
            u32 expected = MEL__EVENT_SIGNALED;
            if (atomic_compare_exchange_strong_explicit(&body->state, &expected, MEL__EVENT_UNSIGNALED, memory_order_acquire, memory_order_relaxed))
                return;
        }
        mel_futex_wait(&body->state, MEL__EVENT_UNSIGNALED);
    }
}

bool mel_thread_event_wait_for(Mel_Thread_Event* e, i64 timeout_ns)
{
    Mel__Thread_Event_Body* body = MEL__EVENT(e);
    if (body->manual_reset)
    {
        if (atomic_load_explicit(&body->state, memory_order_acquire) == MEL__EVENT_SIGNALED)
            return true;
    }
    else
    {
        u32 expected = MEL__EVENT_SIGNALED;
        if (atomic_compare_exchange_strong_explicit(&body->state, &expected, MEL__EVENT_UNSIGNALED, memory_order_acquire, memory_order_relaxed))
            return true;
    }
    if (!mel_futex_wait_for(&body->state, MEL__EVENT_UNSIGNALED, timeout_ns))
        return false;
    if (body->manual_reset)
    {
        return atomic_load_explicit(&body->state, memory_order_acquire) == MEL__EVENT_SIGNALED;
    }
    u32 expected = MEL__EVENT_SIGNALED;
    return atomic_compare_exchange_strong_explicit(&body->state, &expected, MEL__EVENT_UNSIGNALED, memory_order_acquire, memory_order_relaxed);
}

void mel_thread_event_signal(Mel_Thread_Event* e)
{
    Mel__Thread_Event_Body* body = MEL__EVENT(e);
    atomic_store_explicit(&body->state, MEL__EVENT_SIGNALED, memory_order_release);
    if (body->manual_reset)
    {
        mel_futex_wake_all(&body->state);
    }
    else
    {
        mel_futex_wake_one(&body->state);
    }
}

void mel_thread_event_reset(Mel_Thread_Event* e)
{
    Mel__Thread_Event_Body* body = MEL__EVENT(e);
    atomic_store_explicit(&body->state, MEL__EVENT_UNSIGNALED, memory_order_release);
}
