#pragma once

#ifdef _CLANGD
#include "allocator.arena.h"
#endif

#include <string.h>

static inline usize mel__align_forward(usize ptr, usize align)
{
    assert((align & (align - 1)) == 0);
    usize mod = ptr & (align - 1);
    if (mod != 0)
    {
        ptr += align - mod;
    }
    return ptr;
}

static inline void* mel__arena_push_align(Mel_Arena* a, usize size, usize align)
{
    assert(a != NULL);
    assert(size > 0);

    usize aligned_offset = mel__align_forward(a->offset, align);
    assert(aligned_offset + size <= a->size);

    void* ptr = a->base + aligned_offset;
    a->offset = aligned_offset + size;
    return ptr;
}

static inline void* mel__arena_push(Mel_Arena* a, usize size)
{
    return mel__arena_push_align(a, size, 8);
}

static inline void* mel__arena_push_zero_align(Mel_Arena* a, usize size, usize align)
{
    void* ptr = mel__arena_push_align(a, size, align);
    memset(ptr, 0, size);
    return ptr;
}

static inline void* mel__arena_push_zero(Mel_Arena* a, usize size)
{
    return mel__arena_push_zero_align(a, size, 8);
}

static inline void* mel__arena_push_copy(Mel_Arena* a, void* src, usize size)
{
    void* ptr = mel__arena_push(a, size);
    memcpy(ptr, src, size);
    return ptr;
}

#if MEL_ALLOCATOR_ARENA_DEBUG

static inline void* mel__arena_push_align_tracked(Mel_Arena* a, usize size, usize align, const char* file, u32 line)
{
    MEL_UNUSED(file);
    MEL_UNUSED(line);
    void* ptr = mel__arena_push_align(a, size, align);
    a->push_count++;
    if (a->offset > a->peak_used)
    {
        a->peak_used = a->offset;
    }
    return ptr;
}

static inline void* mel__arena_push_tracked(Mel_Arena* a, usize size, const char* file, u32 line)
{
    return mel__arena_push_align_tracked(a, size, 8, file, line);
}

static inline void* mel__arena_push_zero_align_tracked(Mel_Arena* a, usize size, usize align, const char* file, u32 line)
{
    void* ptr = mel__arena_push_align_tracked(a, size, align, file, line);
    memset(ptr, 0, size);
    return ptr;
}

static inline void* mel__arena_push_zero_tracked(Mel_Arena* a, usize size, const char* file, u32 line)
{
    return mel__arena_push_zero_align_tracked(a, size, 8, file, line);
}

static inline void* mel__arena_push_copy_tracked(Mel_Arena* a, void* src, usize size, const char* file, u32 line)
{
    void* ptr = mel__arena_push_align_tracked(a, size, 8, file, line);
    memcpy(ptr, src, size);
    return ptr;
}

#endif


inline static Mel_Arena_Scratch mel_arena_scratch_begin(Mel_Arena* a) { return (Mel_Arena_Scratch){ .arena = a, .checkpoint = a->offset }; }
inline static void              mel_arena_scratch_discard(Mel_Arena_Scratch scratch) { scratch.arena->offset = scratch.checkpoint; }
inline static void              mel_arena_scratch_keep(Mel_Arena_Scratch) {}
