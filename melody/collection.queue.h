#ifndef MEL_COLLECTION_QUEUE_H
#define MEL_COLLECTION_QUEUE_H

#include "allocator.h"
#include <string.h>

#define MEL_QUEUE_INIT_CAP 8

#define Mel_Queue(T) struct { T* items; usize head; usize count; usize capacity; const Mel_Alloc* allocator; }

#define mel_queue_init(q, alloc) do { \
    (q)->items = NULL; \
    (q)->head = 0; \
    (q)->count = 0; \
    (q)->capacity = 0; \
    (q)->allocator = (alloc); \
} while (0)

#define mel_queue_free(q) do { \
    if ((q)->items != NULL) { \
        mel_dealloc((q)->allocator, (q)->items); \
        (q)->items = NULL; \
        (q)->head = 0; \
        (q)->count = 0; \
        (q)->capacity = 0; \
    } \
} while (0)

#define mel_queue_push(q, item) do { \
    if ((q)->count >= (q)->capacity) { \
        usize old_cap = (q)->capacity; \
        usize new_cap = old_cap == 0 ? MEL_QUEUE_INIT_CAP : old_cap * 2; \
        usize elem_size = sizeof(*(q)->items); \
        typeof((q)->items) new_items = mel_alloc((q)->allocator, elem_size * new_cap); \
        if (old_cap > 0) { \
            usize head = (q)->head; \
            usize first_chunk = old_cap - head; \
            if (first_chunk > (q)->count) first_chunk = (q)->count; \
            memcpy(new_items, (q)->items + head, elem_size * first_chunk); \
            if (first_chunk < (q)->count) { \
                memcpy(new_items + first_chunk, (q)->items, elem_size * ((q)->count - first_chunk)); \
            } \
            mel_dealloc((q)->allocator, (q)->items); \
        } \
        (q)->items = new_items; \
        (q)->head = 0; \
        (q)->capacity = new_cap; \
    } \
    (q)->items[((q)->head + (q)->count) % (q)->capacity] = (item); \
    (q)->count++; \
} while (0)

#define mel_queue_pop(q) ( \
    (q)->count--, \
    (q)->head = ((q)->head < (q)->capacity - 1) ? (q)->head + 1 : 0, \
    (q)->items[((q)->head == 0) ? (q)->capacity - 1 : (q)->head - 1] \
)

#define mel_queue_peek(q) ((q)->items[(q)->head])

#define mel_queue_peek_back(q) ((q)->items[((q)->head + (q)->count - 1) % (q)->capacity])

#define mel_queue_count(q) ((q)->count)

#define mel_queue_empty(q) ((q)->count == 0)

#define mel_queue_clear(q) do { \
    (q)->head = 0; \
    (q)->count = 0; \
} while (0)

#define mel_queue_at(q, i) ((q)->items[((q)->head + (i)) % (q)->capacity])

#endif
