#pragma once

#include "allocator.h"
#include <string.h>

#define MEL_DEQUE_INIT_CAP 8

#define Mel_Deque(T) struct { T* items; usize head; usize count; usize capacity; const Mel_Alloc* allocator; }

#define mel_deque_init(dq, alloc) do { \
    (dq)->items = NULL; \
    (dq)->head = 0; \
    (dq)->count = 0; \
    (dq)->capacity = 0; \
    (dq)->allocator = (alloc); \
} while (0)

#define mel_deque_free(dq) do { \
    if ((dq)->items != NULL) { \
        mel_dealloc((dq)->allocator, (dq)->items); \
        (dq)->items = NULL; \
        (dq)->head = 0; \
        (dq)->count = 0; \
        (dq)->capacity = 0; \
    } \
} while (0)

#define mel__deque_grow(dq) do { \
    usize old_cap = (dq)->capacity; \
    usize new_cap = old_cap == 0 ? MEL_DEQUE_INIT_CAP : old_cap * 2; \
    usize elem_size = sizeof(*(dq)->items); \
    typeof((dq)->items) new_items = mel_alloc((dq)->allocator, elem_size * new_cap); \
    if (old_cap > 0 && (dq)->count > 0) { \
        usize head = (dq)->head; \
        if (head + (dq)->count <= old_cap) { \
            memcpy(new_items, (dq)->items + head, elem_size * (dq)->count); \
        } else { \
            usize first_chunk = old_cap - head; \
            memcpy(new_items, (dq)->items + head, elem_size * first_chunk); \
            memcpy(new_items + first_chunk, (dq)->items, elem_size * ((dq)->count - first_chunk)); \
        } \
    } \
    if ((dq)->items != NULL) { \
        mel_dealloc((dq)->allocator, (dq)->items); \
    } \
    (dq)->items = new_items; \
    (dq)->head = 0; \
    (dq)->capacity = new_cap; \
} while (0)

#define mel_deque_push_back(dq, item) do { \
    if ((dq)->count >= (dq)->capacity) { \
        mel__deque_grow(dq); \
    } \
    usize idx = ((dq)->head + (dq)->count) % (dq)->capacity; \
    (dq)->items[idx] = (item); \
    (dq)->count++; \
} while (0)

#define mel_deque_push_front(dq, item) do { \
    if ((dq)->count >= (dq)->capacity) { \
        mel__deque_grow(dq); \
    } \
    (dq)->head = ((dq)->head == 0 ? (dq)->capacity : (dq)->head) - 1; \
    (dq)->items[(dq)->head] = (item); \
    (dq)->count++; \
} while (0)

#define mel_deque_pop_front(dq) ( \
    (dq)->count--, \
    (dq)->head = ((dq)->head + 1) % (dq)->capacity, \
    (dq)->items[((dq)->head == 0 ? (dq)->capacity : (dq)->head) - 1] \
)

#define mel_deque_pop_back(dq) ( \
    (dq)->count--, \
    (dq)->items[((dq)->head + (dq)->count) % (dq)->capacity] \
)

#define mel_deque_peek_front(dq) ((dq)->items[(dq)->head])

#define mel_deque_peek_back(dq) ((dq)->items[((dq)->head + (dq)->count - 1) % (dq)->capacity])

#define mel_deque_at(dq, i) ((dq)->items[((dq)->head + (i)) % (dq)->capacity])

#define mel_deque_count(dq) ((dq)->count)

#define mel_deque_empty(dq) ((dq)->count == 0)

#define mel_deque_clear(dq) do { \
    (dq)->head = 0; \
    (dq)->count = 0; \
} while (0)
