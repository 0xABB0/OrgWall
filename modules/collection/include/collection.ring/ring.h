#pragma once

#include "ring.fwd.h"
#include <allocator/allocator.h>

#define mel_ring_init(r, cap, alloc) do { \
    (r)->items = mel_alloc((alloc), sizeof(*(r)->items) * (cap)); \
    (r)->head = 0; \
    (r)->count = 0; \
    (r)->capacity = (cap); \
    (r)->allocator = (alloc); \
} while (0)

#define mel_ring_free(r) do { \
    if ((r)->items != NULL) { \
        mel_dealloc((r)->allocator, (r)->items); \
        (r)->items = NULL; \
        (r)->count = 0; \
        (r)->capacity = 0; \
        (r)->head = 0; \
    } \
} while (0)

#define mel_ring_push(r, item) do { \
    usize mel__tail = ((r)->head + (r)->count) % (r)->capacity; \
    if ((r)->count == (r)->capacity) { \
        (r)->items[mel__tail] = (item); \
        (r)->head = ((r)->head + 1) % (r)->capacity; \
    } else { \
        (r)->items[mel__tail] = (item); \
        (r)->count++; \
    } \
} while (0)

#define mel_ring_pop(r) \
    ({ \
        assert((r)->count > 0); \
        typeof((r)->items[0]) mel__val = (r)->items[(r)->head]; \
        (r)->head = ((r)->head + 1) % (r)->capacity; \
        (r)->count--; \
        mel__val; \
    })

#define mel_ring_peek(r) \
    ({ \
        assert((r)->count > 0); \
        (r)->items[(r)->head]; \
    })

#define mel_ring_peek_back(r) \
    ({ \
        assert((r)->count > 0); \
        (r)->items[((r)->head + (r)->count - 1) % (r)->capacity]; \
    })

#define mel_ring_at(r, i) \
    ({ \
        assert((usize)(i) < (r)->count); \
        (r)->items[((r)->head + (usize)(i)) % (r)->capacity]; \
    })

#define mel_ring_count(r) ((r)->count)

#define mel_ring_capacity(r) ((r)->capacity)

#define mel_ring_full(r) ((r)->count == (r)->capacity)

#define mel_ring_empty(r) ((r)->count == 0)

#define mel_ring_clear(r) do { \
    (r)->count = 0; \
    (r)->head = 0; \
} while (0)
