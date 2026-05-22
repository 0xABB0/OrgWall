#include <collection.mpsc/mpsc.h>

void mel_mpsc_init(Mel_Mpsc* q)
{
    assert(q != NULL);

    atomic_store_explicit(&q->stub.next, NULL, memory_order_relaxed);
    q->consumer_head = &q->stub;
    atomic_store_explicit(&q->producer_tail, &q->stub, memory_order_relaxed);

#if MEL_COLLECTION_MPSC_DEBUG
    atomic_store_explicit(&q->push_count, 0, memory_order_relaxed);
    atomic_store_explicit(&q->pop_count, 0, memory_order_relaxed);
    q->name = NULL;
#endif
}
