#ifndef MEL_ARRAY_H
#define MEL_ARRAY_H

#include "memory.h"
#include <string.h>

#define MEL_DA_INIT_CAP 8

#define Mel_Array(T) struct { T* items; usize count; usize capacity; const Mel_Alloc* allocator; }

#define mel_array_init(arr, alloc) do { \
    (arr)->items = NULL; \
    (arr)->count = 0; \
    (arr)->capacity = 0; \
    (arr)->allocator = (alloc); \
} while (0)

#define mel_array_free(arr) do { \
    if ((arr)->items != NULL) { \
        mel_free((arr)->allocator, (arr)->items); \
        (arr)->items = NULL; \
        (arr)->count = 0; \
        (arr)->capacity = 0; \
    } \
} while (0)

#define mel_array_push(arr, item) do { \
    if ((arr)->count >= (arr)->capacity) { \
        usize new_cap = (arr)->capacity == 0 ? MEL_DA_INIT_CAP : (arr)->capacity * 2; \
        usize new_size = sizeof(*(arr)->items) * new_cap; \
        if ((arr)->items == NULL) { \
            (arr)->items = mel_malloc((arr)->allocator, new_size); \
        } else { \
            (arr)->items = mel_realloc((arr)->allocator, (arr)->items, new_size); \
        } \
        (arr)->capacity = new_cap; \
    } \
    (arr)->items[(arr)->count++] = (item); \
} while (0)

#define mel_array_pop(arr) ((arr)->items[--(arr)->count])

#define mel_array_last(arr) ((arr)->items[(arr)->count - 1])

#define mel_array_clear(arr) ((arr)->count = 0)

#define mel_array_reserve(arr, n) do { \
    if ((n) > (arr)->capacity) { \
        usize new_size = sizeof(*(arr)->items) * (n); \
        if ((arr)->items == NULL) { \
            (arr)->items = mel_malloc((arr)->allocator, new_size); \
        } else { \
            (arr)->items = mel_realloc((arr)->allocator, (arr)->items, new_size); \
        } \
        (arr)->capacity = (n); \
    } \
} while (0)

#define mel_array_remove_unordered(arr, idx) do { \
    assert((idx) < (arr)->count); \
    (arr)->items[(idx)] = (arr)->items[--(arr)->count]; \
} while (0)

#define mel_array_remove_ordered(arr, idx) do { \
    assert((idx) < (arr)->count); \
    memmove(&(arr)->items[(idx)], &(arr)->items[(idx) + 1], \
            sizeof(*(arr)->items) * ((arr)->count - (idx) - 1)); \
    (arr)->count--; \
} while (0)

#endif
