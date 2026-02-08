#include "allocator.heap.h"
#include "allocator.h"
#include <stdlib.h>

extern Mel_Mem_Fail_Cb mel__get_fail_cb(void);

static void* heap_alloc_cb(void* ptr, usize size, u32 align,
                           const char* file, const char* func, u32 line,
                           void* user_data)
{
    MEL_UNUSED(align);
    MEL_UNUSED(user_data);

    Mel_Mem_Fail_Cb fail_cb = mel__get_fail_cb();

    if (ptr == NULL && size > 0)
    {
        void* result = malloc(size);
        if (!result && fail_cb)
        {
            fail_cb(file, func, line, size);
        }
        return result;
    }

    if (ptr != NULL && size > 0)
    {
        void* result = realloc(ptr, size);
        if (!result && fail_cb)
        {
            fail_cb(file, func, line, size);
        }
        return result;
    }

    if (ptr != NULL && size == 0)
    {
        free(ptr);
        return NULL;
    }

    return NULL;
}

static const Mel_Alloc s_heap_alloc = {
    .alloc_cb = heap_alloc_cb,
    .user_data = NULL
};

const Mel_Alloc* mel_alloc_heap(void)
{
    return &s_heap_alloc;
}
