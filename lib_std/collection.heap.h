#pragma once

#include "allocator.h"

#define MEL_HEAP_INIT_CAP 8

#define Mel_Heap(T) struct { T* items; usize count; usize capacity; const Mel_Alloc* allocator; }

#define mel_heap_init(h, alloc) do { \
    (h)->items = NULL; \
    (h)->count = 0; \
    (h)->capacity = 0; \
    (h)->allocator = (alloc); \
} while (0)

#define mel_heap_free(h) do { \
    if ((h)->items != NULL) { \
        mel_dealloc((h)->allocator, (h)->items); \
        (h)->items = NULL; \
        (h)->count = 0; \
        (h)->capacity = 0; \
    } \
} while (0)

#define mel_heap_push(h, item, lt_expr) do { \
    if ((h)->count >= (h)->capacity) { \
        usize mel__new_cap = (h)->capacity == 0 ? MEL_HEAP_INIT_CAP : (h)->capacity * 2; \
        usize mel__new_size = sizeof(*(h)->items) * mel__new_cap; \
        if ((h)->items == NULL) { \
            (h)->items = mel_alloc((h)->allocator, mel__new_size); \
        } else { \
            (h)->items = mel_realloc((h)->allocator, (h)->items, mel__new_size); \
        } \
        (h)->capacity = mel__new_cap; \
    } \
    (h)->items[(h)->count] = (item); \
    usize mel__i = (h)->count++; \
    while (mel__i > 0) { \
        usize mel__parent = (mel__i - 1) / 2; \
        typeof((h)->items[0]) a = (h)->items[mel__i]; \
        typeof((h)->items[0]) b = (h)->items[mel__parent]; \
        if (!(lt_expr)) break; \
        (h)->items[mel__i] = (h)->items[mel__parent]; \
        (h)->items[mel__parent] = a; \
        mel__i = mel__parent; \
    } \
} while (0)

#define mel_heap_pop(h, lt_expr) mel__heap_pop_impl(h, lt_expr)

#define mel__heap_pop_impl(h, lt_expr) __extension__ ({ \
    typeof((h)->items[0]) mel__result = (h)->items[0]; \
    (h)->count--; \
    if ((h)->count > 0) { \
        (h)->items[0] = (h)->items[(h)->count]; \
        usize mel__i = 0; \
        for (;;) { \
            usize mel__left = 2 * mel__i + 1; \
            usize mel__right = 2 * mel__i + 2; \
            usize mel__target = mel__i; \
            if (mel__left < (h)->count) { \
                typeof((h)->items[0]) a = (h)->items[mel__left]; \
                typeof((h)->items[0]) b = (h)->items[mel__target]; \
                if (lt_expr) mel__target = mel__left; \
            } \
            if (mel__right < (h)->count) { \
                typeof((h)->items[0]) a = (h)->items[mel__right]; \
                typeof((h)->items[0]) b = (h)->items[mel__target]; \
                if (lt_expr) mel__target = mel__right; \
            } \
            if (mel__target == mel__i) break; \
            typeof((h)->items[0]) mel__tmp = (h)->items[mel__i]; \
            (h)->items[mel__i] = (h)->items[mel__target]; \
            (h)->items[mel__target] = mel__tmp; \
            mel__i = mel__target; \
        } \
    } \
    mel__result; \
})

#define mel_heap_peek(h) ((h)->items[0])

#define mel_heap_count(h) ((h)->count)

#define mel_heap_empty(h) ((h)->count == 0)

#define mel_heap_clear(h) ((h)->count = 0)
