#pragma once

#ifdef _CLANGD
#include "mpsc.h"
#endif

inline static void mel_mpsc_push(Mel_Mpsc* q, Mel_Mpsc_Node* node)
{
    assert(q    != NULL);
    assert(node != NULL);

    atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
    Mel_Mpsc_Node* prev = atomic_exchange_explicit(&q->producer_tail, node,
                                                   memory_order_acq_rel);
    atomic_store_explicit(&prev->next, node, memory_order_release);

#if MEL_COLLECTION_MPSC_DEBUG
    atomic_fetch_add_explicit(&q->push_count, 1, memory_order_relaxed);
#endif
}

inline static Mel_Mpsc_Node* mel_mpsc_pop(Mel_Mpsc* q)
{
    assert(q != NULL);

    Mel_Mpsc_Node* head = q->consumer_head;
    Mel_Mpsc_Node* next = atomic_load_explicit(&head->next, memory_order_acquire);

    if (head == &q->stub) {
        if (next == NULL) return NULL;
        q->consumer_head = next;
        head = next;
        next = atomic_load_explicit(&head->next, memory_order_acquire);
    }

    if (next != NULL) {
        q->consumer_head = next;
#if MEL_COLLECTION_MPSC_DEBUG
        atomic_fetch_add_explicit(&q->pop_count, 1, memory_order_relaxed);
#endif
        return head;
    }

    Mel_Mpsc_Node* tail = atomic_load_explicit(&q->producer_tail, memory_order_acquire);
    if (head != tail) return NULL;

    mel_mpsc_push(q, &q->stub);

    next = atomic_load_explicit(&head->next, memory_order_acquire);
    if (next != NULL) {
        q->consumer_head = next;
#if MEL_COLLECTION_MPSC_DEBUG
        atomic_fetch_add_explicit(&q->pop_count, 1, memory_order_relaxed);
#endif
        return head;
    }

    return NULL;
}
