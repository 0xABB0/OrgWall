#pragma once

#include "types.h"

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
#define mel_malloc(a, size)                    mel__malloc((a), (size), __FILE__, __func__, __LINE__)
#define mel_realloc(a, ptr, size)              mel__realloc((a), (ptr), (size), __FILE__, __func__, __LINE__)
#define mel_free(a, ptr)                       mel__free((a), (ptr), __FILE__, __func__, __LINE__)
#define mel_calloc(a, size)                    mel__calloc((a), (size), __FILE__, __func__, __LINE__)
#define mel_aligned_malloc(a, size, align)     mel__aligned_malloc((a), (size), (align), __FILE__, __func__, __LINE__)
#define mel_aligned_realloc(a, ptr, size, align) mel__aligned_realloc((a), (ptr), (size), (align), __FILE__, __func__, __LINE__)
#define mel_aligned_free(a, ptr, align)        mel__aligned_free((a), (ptr), (align), __FILE__, __func__, __LINE__)
#else
#define mel_malloc(a, size)                    mel__malloc((a), (size), NULL, NULL, 0)
#define mel_realloc(a, ptr, size)              mel__realloc((a), (ptr), (size), NULL, NULL, 0)
#define mel_free(a, ptr)                       mel__free((a), (ptr), NULL, NULL, 0)
#define mel_calloc(a, size)                    mel__calloc((a), (size), NULL, NULL, 0)
#define mel_aligned_malloc(a, size, align)     mel__aligned_malloc((a), (size), (align), NULL, NULL, 0)
#define mel_aligned_realloc(a, ptr, size, align) mel__aligned_realloc((a), (ptr), (size), (align), NULL, NULL, 0)
#define mel_aligned_free(a, ptr, align)        mel__aligned_free((a), (ptr), (align), NULL, NULL, 0)
#endif

#define mel_alloc_type(a, T)      ((T*)mel_malloc((a), sizeof(T)))
#define mel_alloc_array(a, T, n)  ((T*)mel_calloc((a), sizeof(T) * (n)))

const Mel_Alloc* mel_alloc_malloc(void);

typedef void (*Mel_Leak_Report_Cb)(const char* file, const char* func, u32 line, usize size, void* user_data);

const Mel_Alloc* mel_alloc_leak_detect(void);
void             mel_dump_leaks(Mel_Leak_Report_Cb cb, void* user_data);

#include "allocator.inl"

