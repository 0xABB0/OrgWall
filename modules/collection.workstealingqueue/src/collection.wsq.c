#include "collection.wsq.h"
#include "allocator.h"

static Mel_Wsq_Array* mel__wsq_array_create(const Mel_Alloc* alloc, i64 capacity)
{
    Mel_Wsq_Array* a = mel_alloc(alloc, sizeof(Mel_Wsq_Array) + sizeof(_Atomic(void*)) * (usize)capacity);
    a->capacity = capacity;
    for (i64 i = 0; i < capacity; i++)
        atomic_store_explicit(&a->slots[i], NULL, memory_order_relaxed);
    return a;
}

static void mel__wsq_garbage_push(Mel_Wsq* q, Mel_Wsq_Array* old)
{
    if (q->garbage_count >= q->garbage_capacity)
    {
        i64 new_cap = q->garbage_capacity == 0 ? 8 : q->garbage_capacity * 2;
        Mel_Wsq_Array** new_buf = mel_alloc(q->alloc, sizeof(Mel_Wsq_Array*) * (usize)new_cap);
        if (q->garbage)
        {
            for (i64 i = 0; i < q->garbage_count; i++)
                new_buf[i] = q->garbage[i];
            mel_dealloc(q->alloc, q->garbage);
        }
        q->garbage = new_buf;
        q->garbage_capacity = new_cap;
    }
    q->garbage[q->garbage_count++] = old;
}

static void mel__wsq_grow(Mel_Wsq* q, Mel_Wsq_Array* old, i64 bottom, i64 top)
{
    Mel_Wsq_Array* a = mel__wsq_array_create(q->alloc, old->capacity * 2);
    for (i64 i = top; i < bottom; i++)
    {
        void* item = atomic_load_explicit(&old->slots[i % old->capacity], memory_order_relaxed);
        atomic_store_explicit(&a->slots[i % a->capacity], item, memory_order_relaxed);
    }
    mel__wsq_garbage_push(q, old);
    atomic_store_explicit(&q->array, a, memory_order_release);
}

Mel_Wsq mel_wsq_create_opt(const Mel_Alloc* alloc, Mel_Wsq_Opt opt)
{
    i64 cap = opt.initial_capacity > 0 ? opt.initial_capacity : 64;
    if (cap < 2) cap = 2;

    i64 rounded = 1;
    while (rounded < cap) rounded *= 2;

    Mel_Wsq q = {0};
    q.alloc = alloc;
    atomic_store_explicit(&q.top, 0, memory_order_relaxed);
    atomic_store_explicit(&q.bottom, 0, memory_order_relaxed);
    atomic_store_explicit(&q.array, mel__wsq_array_create(alloc, rounded), memory_order_relaxed);
    return q;
}

void mel_wsq_destroy(Mel_Wsq* q)
{
    Mel_Wsq_Array* a = atomic_load_explicit(&q->array, memory_order_relaxed);
    mel_dealloc(q->alloc, a);
    for (i64 i = 0; i < q->garbage_count; i++)
        mel_dealloc(q->alloc, q->garbage[i]);
    if (q->garbage)
        mel_dealloc(q->alloc, q->garbage);
    *q = (Mel_Wsq){0};
}

void mel_wsq_push(Mel_Wsq* q, void* item)
{
    assert(item != NULL && item != MEL_WSQ_EMPTY && item != MEL_WSQ_ABORT);

    i64 b = atomic_load_explicit(&q->bottom, memory_order_relaxed);
    i64 t = atomic_load_explicit(&q->top, memory_order_acquire);
    Mel_Wsq_Array* a = atomic_load_explicit(&q->array, memory_order_relaxed);

    if (b - t >= a->capacity)
    {
        mel__wsq_grow(q, a, b, t);
        a = atomic_load_explicit(&q->array, memory_order_relaxed);
    }

    atomic_store_explicit(&a->slots[b % a->capacity], item, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
}

void* mel_wsq_pop(Mel_Wsq* q)
{
    i64 b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    Mel_Wsq_Array* a = atomic_load_explicit(&q->array, memory_order_relaxed);
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);

    i64 t = atomic_load_explicit(&q->top, memory_order_relaxed);
    if (t <= b)
    {
        void* item = atomic_load_explicit(&a->slots[b % a->capacity], memory_order_relaxed);
        if (t == b)
        {
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1,
                memory_order_seq_cst, memory_order_relaxed))
            {
                atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
                return MEL_WSQ_EMPTY;
            }
            atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
        }
        return item;
    }

    atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
    return MEL_WSQ_EMPTY;
}

void* mel_wsq_steal(Mel_Wsq* q)
{
    i64 t = atomic_load_explicit(&q->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    i64 b = atomic_load_explicit(&q->bottom, memory_order_acquire);

    if (t >= b)
        return MEL_WSQ_EMPTY;

    Mel_Wsq_Array* a = atomic_load_explicit(&q->array, memory_order_acquire);
    void* item = atomic_load_explicit(&a->slots[t % a->capacity], memory_order_relaxed);

    if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1,
        memory_order_seq_cst, memory_order_relaxed))
        return MEL_WSQ_ABORT;

    return item;
}

i64 mel_wsq_size(Mel_Wsq* q)
{
    i64 b = atomic_load_explicit(&q->bottom, memory_order_relaxed);
    i64 t = atomic_load_explicit(&q->top, memory_order_relaxed);
    i64 s = b - t;
    return s < 0 ? 0 : s;
}
