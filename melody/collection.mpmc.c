#include "collection.mpmc.h"
#include "allocator.h"

void mel_mpmc_init(Mel_Mpmc* q, u64 capacity, const Mel_Alloc* alloc)
{
    assert(q);
    assert(alloc);
    assert(capacity > 0);
    assert((capacity & (capacity - 1)) == 0);

    q->alloc = alloc;
    q->mask = capacity - 1;
    q->cells = mel_alloc(alloc, sizeof(Mel__Mpmc_Cell) * capacity);

    for (u64 i = 0; i < capacity; i++)
    {
        atomic_store_explicit(&q->cells[i].sequence, i, memory_order_relaxed);
        q->cells[i].data = NULL;
    }

    atomic_store_explicit(&q->head, 0, memory_order_relaxed);
    atomic_store_explicit(&q->tail, 0, memory_order_relaxed);
}

void mel_mpmc_free(Mel_Mpmc* q)
{
    assert(q);
    if (q->cells)
    {
        mel_dealloc(q->alloc, q->cells);
        q->cells = NULL;
    }
}

bool mel_mpmc_push(Mel_Mpmc* q, void* data)
{
    u64 pos = atomic_load_explicit(&q->tail, memory_order_relaxed);

    for (;;)
    {
        Mel__Mpmc_Cell* cell = &q->cells[pos & q->mask];
        u64 seq = atomic_load_explicit(&cell->sequence, memory_order_acquire);
        i64 diff = (i64)seq - (i64)pos;

        if (diff == 0)
        {
            if (atomic_compare_exchange_weak_explicit(&q->tail, &pos, pos + 1,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed))
            {
                cell->data = data;
                atomic_store_explicit(&cell->sequence, pos + 1, memory_order_release);
                return true;
            }
        }
        else if (diff < 0)
        {
            return false;
        }
        else
        {
            pos = atomic_load_explicit(&q->tail, memory_order_relaxed);
        }
    }
}

bool mel_mpmc_pop(Mel_Mpmc* q, void** out)
{
    u64 pos = atomic_load_explicit(&q->head, memory_order_relaxed);

    for (;;)
    {
        Mel__Mpmc_Cell* cell = &q->cells[pos & q->mask];
        u64 seq = atomic_load_explicit(&cell->sequence, memory_order_acquire);
        i64 diff = (i64)seq - (i64)(pos + 1);

        if (diff == 0)
        {
            if (atomic_compare_exchange_weak_explicit(&q->head, &pos, pos + 1,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed))
            {
                *out = cell->data;
                atomic_store_explicit(&cell->sequence, pos + q->mask + 1, memory_order_release);
                return true;
            }
        }
        else if (diff < 0)
        {
            return false;
        }
        else
        {
            pos = atomic_load_explicit(&q->head, memory_order_relaxed);
        }
    }
}
