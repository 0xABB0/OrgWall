#include <async.signal/signal.h>

static _Atomic(u32) s_generation = 1;

static struct {
    Mel__Park_Node* park_pool;
    u32 park_pool_size;
    void (*schedule_fiber)(u16 park_index);
    void (*wake_workers)(i32 count);
} s_rt = {0};

void mel__signal_init_runtime(Mel__Signal_Runtime rt)
{
    s_rt.park_pool       = rt.park_pool;
    s_rt.park_pool_size  = rt.park_pool_size;
    s_rt.schedule_fiber  = rt.schedule_fiber;
    s_rt.wake_workers    = rt.wake_workers;
}

u32 mel__signal_next_generation(void)
{
    return atomic_fetch_add_explicit(&s_generation, 1, memory_order_relaxed);
}

static void mel__signal_wake_list(u16 head)
{
    if (head == MEL_SIGNAL_NULL_INDEX)
        return;

    assert(s_rt.park_pool);
    assert(s_rt.schedule_fiber);
    assert(s_rt.wake_workers);

    i32 count = 0;
    u16 idx = head;
    while (idx != MEL_SIGNAL_NULL_INDEX)
    {
        assert(idx < s_rt.park_pool_size);
        Mel__Park_Node* node = &s_rt.park_pool[idx];
        u16 this_idx = idx;
        idx = node->next;
        s_rt.schedule_fiber(this_idx);
        count++;
    }

    s_rt.wake_workers(count);
}

static void mel__signal_wake_one(u16 idx)
{
    if (idx == MEL_SIGNAL_NULL_INDEX)
        return;

    assert(s_rt.park_pool);
    assert(s_rt.schedule_fiber);
    assert(s_rt.wake_workers);
    assert(idx < s_rt.park_pool_size);

    s_rt.schedule_fiber(idx);
    s_rt.wake_workers(1);
}

void mel_signal_set(Mel_Signal* s)
{
    for (;;)
    {
        i32 old = atomic_load_explicit(&s->state, memory_order_relaxed);
        u16 counter = mel__signal_counter(old);

        if (counter > 0)
            return;

        u16 head = mel__signal_head(old);
        i32 desired = mel__signal_pack(1, head);

        if (atomic_compare_exchange_weak_explicit(&s->state, &old, desired,
                                                   memory_order_release,
                                                   memory_order_relaxed))
        {
            s->generation = atomic_fetch_add_explicit(&s_generation, 1, memory_order_relaxed);
            return;
        }
    }
}

void mel_signal_clear(Mel_Signal* s)
{
    i32 old = atomic_exchange_explicit(&s->state,
                                       mel__signal_pack(0, MEL_SIGNAL_NULL_INDEX),
                                       memory_order_acq_rel);
    u16 head = mel__signal_head(old);
    mel__signal_wake_list(head);
}

void mel_counter_increment(Mel_Counter* c)
{
    for (;;)
    {
        i32 old = atomic_load_explicit(&c->signal.state, memory_order_relaxed);
        u16 counter = mel__signal_counter(old);
        u16 head = mel__signal_head(old);

        assert(counter < 0xFFFE);

        i32 desired = mel__signal_pack(counter + 1, head);

        if (atomic_compare_exchange_weak_explicit(&c->signal.state, &old, desired,
                                                   memory_order_release,
                                                   memory_order_relaxed))
        {
            if (counter == 0)
                c->signal.generation = atomic_fetch_add_explicit(&s_generation, 1, memory_order_relaxed);
            return;
        }
    }
}

void mel_counter_decrement(Mel_Counter* c)
{
    for (;;)
    {
        i32 old = atomic_load_explicit(&c->signal.state, memory_order_acquire);
        u16 counter = mel__signal_counter(old);
        u16 head = mel__signal_head(old);

        assert(counter > 0);

        if (counter == 1)
        {
            i32 desired = mel__signal_pack(0, MEL_SIGNAL_NULL_INDEX);
            if (atomic_compare_exchange_weak_explicit(&c->signal.state, &old, desired,
                                                       memory_order_acq_rel,
                                                       memory_order_relaxed))
            {
                mel__signal_wake_list(head);
                return;
            }
        }
        else
        {
            i32 desired = mel__signal_pack(counter - 1, head);
            if (atomic_compare_exchange_weak_explicit(&c->signal.state, &old, desired,
                                                       memory_order_release,
                                                       memory_order_relaxed))
                return;
        }
    }
}

void mel_counter_wait(Mel_Counter* c)
{
    mel_signal_wait(&c->signal);
}

void mel_mutex_enter(Mel_Mutex* m)
{
    mel_signal_wait_and_set(&m->signal);
}

void mel_mutex_exit(Mel_Mutex* m)
{
    for (;;)
    {
        i32 old = atomic_load_explicit(&m->signal.state, memory_order_acquire);
        u16 head = mel__signal_head(old);

        assert(mel__signal_counter(old) > 0);

        if (head == MEL_SIGNAL_NULL_INDEX)
        {
            i32 desired = mel__signal_pack(0, MEL_SIGNAL_NULL_INDEX);
            if (atomic_compare_exchange_weak_explicit(&m->signal.state, &old, desired,
                                                       memory_order_release,
                                                       memory_order_relaxed))
                return;
        }
        else
        {
            assert(head < s_rt.park_pool_size);
            u16 next = s_rt.park_pool[head].next;
            i32 desired = mel__signal_pack(0, next);
            if (atomic_compare_exchange_weak_explicit(&m->signal.state, &old, desired,
                                                       memory_order_acq_rel,
                                                       memory_order_relaxed))
            {
                mel__signal_wake_one(head);
                return;
            }
        }
    }
}
