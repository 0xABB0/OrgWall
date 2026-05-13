#pragma once

#include "core.types.h"

#include <string.h>

typedef struct Mel_Alloc Mel_Alloc;

typedef void* (*Mel_Alloc_Cb)(void* ptr, usize size, u32 align,
                              const char* file, const char* func, u32 line,
                              void* user_data);

struct Mel_Alloc
{
    Mel_Alloc_Cb alloc_cb;
    void* user_data;
};

typedef void (*Mel_Mem_Fail_Cb)(const char* file, const char* func, u32 line, usize size);

void mel_mem_set_fail_callback(Mel_Mem_Fail_Cb cb);

#ifdef MEL_CONFIG_DEBUG_ALLOCATOR
#define mel_alloc(a, size)                     mel__alloc((a), (size), __FILE__, __func__, __LINE__)
#define mel_realloc(a, ptr, size)              mel__realloc((a), (ptr), (size), __FILE__, __func__, __LINE__)
#define mel_dealloc(a, ptr)                    mel__dealloc((a), (ptr), __FILE__, __func__, __LINE__)
#define mel_calloc(a, size)                    mel__calloc((a), (size), __FILE__, __func__, __LINE__)
#define mel_aligned_alloc(a, size, align)      mel__aligned_alloc((a), (size), (align), __FILE__, __func__, __LINE__)
#define mel_aligned_realloc(a, ptr, size, align) mel__aligned_realloc((a), (ptr), (size), (align), __FILE__, __func__, __LINE__)
#define mel_aligned_dealloc(a, ptr, align)     mel__aligned_dealloc((a), (ptr), (align), __FILE__, __func__, __LINE__)
#else
#define mel_alloc(a, size)                     mel__alloc((a), (size), NULL, NULL, 0)
#define mel_realloc(a, ptr, size)              mel__realloc((a), (ptr), (size), NULL, NULL, 0)
#define mel_dealloc(a, ptr)                    mel__dealloc((a), (ptr), NULL, NULL, 0)
#define mel_calloc(a, size)                    mel__calloc((a), (size), NULL, NULL, 0)
#define mel_aligned_alloc(a, size, align)      mel__aligned_alloc((a), (size), (align), NULL, NULL, 0)
#define mel_aligned_realloc(a, ptr, size, align) mel__aligned_realloc((a), (ptr), (size), (align), NULL, NULL, 0)
#define mel_aligned_dealloc(a, ptr, align)     mel__aligned_dealloc((a), (ptr), (align), NULL, NULL, 0)
#endif

#define mel_alloc_type(a, T)      ((T*)mel_alloc((a), sizeof(T)))
#define mel_alloc_array(a, T, n)  ((T*)mel_calloc((a), sizeof(T) * (n)))

#include "allocator.inl"
