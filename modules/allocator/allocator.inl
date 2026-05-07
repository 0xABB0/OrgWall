#pragma once

#ifdef _CLANGD
#include "allocator.h"
#endif


static inline void* mel__alloc(const Mel_Alloc* a, usize size,
                                const char* file, const char* func, u32 line)
{
    assert(a != NULL);
    assert(a->alloc_cb != NULL);
    assert(size > 0);
    return a->alloc_cb(NULL, size, 0, file, func, line, a->user_data);
}

static inline void* mel__realloc(const Mel_Alloc* a, void* ptr, usize size,
                                 const char* file, const char* func, u32 line)
{
    assert(a != NULL);
    assert(a->alloc_cb != NULL);
    assert(ptr != NULL);
    assert(size > 0);
    return a->alloc_cb(ptr, size, 0, file, func, line, a->user_data);
}

static inline void mel__dealloc(const Mel_Alloc* a, void* ptr,
                             const char* file, const char* func, u32 line)
{
    assert(a != NULL);
    assert(a->alloc_cb != NULL);
    if (ptr != NULL)
    {
        a->alloc_cb(ptr, 0, 0, file, func, line, a->user_data);
    }
}

static inline void* mel__calloc(const Mel_Alloc* a, usize size,
                                const char* file, const char* func, u32 line)
{
    void* ptr = mel__alloc(a, size, file, func, line);
    if (ptr)
    {
        memset(ptr, 0, size);
    }
    return ptr;
}

static inline void* mel__aligned_alloc(const Mel_Alloc* a, usize size, u32 align,
                                        const char* file, const char* func, u32 line)
{
    assert(a != NULL);
    assert(a->alloc_cb != NULL);
    assert(size > 0);
    assert(align > 0 && (align & (align - 1)) == 0);
    return a->alloc_cb(NULL, size, align, file, func, line, a->user_data);
}

static inline void* mel__aligned_realloc(const Mel_Alloc* a, void* ptr, usize size, u32 align,
                                         const char* file, const char* func, u32 line)
{
    assert(a != NULL);
    assert(a->alloc_cb != NULL);
    assert(ptr != NULL);
    assert(size > 0);
    assert(align > 0 && (align & (align - 1)) == 0);
    return a->alloc_cb(ptr, size, align, file, func, line, a->user_data);
}

static inline void mel__aligned_dealloc(const Mel_Alloc* a, void* ptr, u32 align,
                                     const char* file, const char* func, u32 line)
{
    assert(a != NULL);
    assert(a->alloc_cb != NULL);
    if (ptr != NULL)
    {
        a->alloc_cb(ptr, 0, align, file, func, line, a->user_data);
    }
}
