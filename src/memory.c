#include "memory.h"
#include "allocator.h"
#include "allocator_arena.h"
#include "allocator_tracking.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static Mel_Mem_Fail_Cb s_fail_cb = NULL;

void mel_mem_set_fail_callback(Mel_Mem_Fail_Cb cb)
{
    s_fail_cb = cb;
}

static void* heap_alloc_cb(void* ptr, usize size, u32 align,
                           const char* file, const char* func, u32 line,
                           void* user_data)
{
    MEL_UNUSED(align);
    MEL_UNUSED(file);
    MEL_UNUSED(func);
    MEL_UNUSED(line);
    MEL_UNUSED(user_data);

    if (ptr == NULL && size > 0)
    {
        void* result = malloc(size);
        if (!result && s_fail_cb)
        {
            s_fail_cb(file, func, line, size);
        }
        return result;
    }

    if (ptr != NULL && size > 0)
    {
        void* result = realloc(ptr, size);
        if (!result && s_fail_cb)
        {
            s_fail_cb(file, func, line, size);
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

const Mel_Alloc* mel_alloc_malloc(void)
{
    return &s_heap_alloc;
}

typedef struct Mel_Leak_Header Mel_Leak_Header;

struct Mel_Leak_Header
{
    Mel_Leak_Header* prev;
    Mel_Leak_Header* next;
    const char* file;
    const char* func;
    u32 line;
    usize size;
};

#define MEL_LEAK_FREED_MAGIC (~(usize)0)

static Mel_Leak_Header s_leak_sentinel = {
    .prev = &s_leak_sentinel,
    .next = &s_leak_sentinel,
    .file = NULL,
    .func = NULL,
    .line = 0,
    .size = 0
};

static void* leak_detect_cb(void* ptr, usize size, u32 align,
                            const char* file, const char* func, u32 line,
                            void* user_data)
{
    MEL_UNUSED(align);
    MEL_UNUSED(user_data);

    if (ptr == NULL && size > 0)
    {
        Mel_Leak_Header* header = (Mel_Leak_Header*)malloc(sizeof(Mel_Leak_Header) + size);
        if (!header)
        {
            if (s_fail_cb) s_fail_cb(file, func, line, size);
            return NULL;
        }
        header->file = file;
        header->func = func;
        header->line = line;
        header->size = size;

        header->next = s_leak_sentinel.next;
        header->prev = &s_leak_sentinel;
        s_leak_sentinel.next->prev = header;
        s_leak_sentinel.next = header;

        return (void*)(header + 1);
    }

    if (ptr != NULL && size > 0)
    {
        Mel_Leak_Header* header = ((Mel_Leak_Header*)ptr) - 1;
        assert(header->size != MEL_LEAK_FREED_MAGIC);

        header->prev->next = header->next;
        header->next->prev = header->prev;

        Mel_Leak_Header* new_header = (Mel_Leak_Header*)realloc(header, sizeof(Mel_Leak_Header) + size);
        if (!new_header)
        {
            header->next = s_leak_sentinel.next;
            header->prev = &s_leak_sentinel;
            s_leak_sentinel.next->prev = header;
            s_leak_sentinel.next = header;
            if (s_fail_cb) s_fail_cb(file, func, line, size);
            return NULL;
        }
        new_header->file = file;
        new_header->func = func;
        new_header->line = line;
        new_header->size = size;

        new_header->next = s_leak_sentinel.next;
        new_header->prev = &s_leak_sentinel;
        s_leak_sentinel.next->prev = new_header;
        s_leak_sentinel.next = new_header;

        return (void*)(new_header + 1);
    }

    if (ptr != NULL && size == 0)
    {
        Mel_Leak_Header* header = ((Mel_Leak_Header*)ptr) - 1;
        assert(header->size != MEL_LEAK_FREED_MAGIC);

        header->prev->next = header->next;
        header->next->prev = header->prev;
        header->size = MEL_LEAK_FREED_MAGIC;
        free(header);
        return NULL;
    }

    return NULL;
}

static const Mel_Alloc s_leak_detect_alloc = {
    .alloc_cb = leak_detect_cb,
    .user_data = NULL
};

const Mel_Alloc* mel_alloc_leak_detect(void)
{
    return &s_leak_detect_alloc;
}

void mel_dump_leaks(Mel_Leak_Report_Cb cb, void* user_data)
{
    Mel_Leak_Header* h = s_leak_sentinel.next;
    while (h != &s_leak_sentinel)
    {
        cb(h->file, h->func, h->line, h->size, user_data);
        h = h->next;
    }
}

static usize align_forward(usize ptr, usize align)
{
    assert((align & (align - 1)) == 0);
    usize mod = ptr & (align - 1);
    if (mod != 0)
    {
        ptr += align - mod;
    }
    return ptr;
}

void mel_arena_init(Mel_Arena* arena, void* buffer, usize size)
{
    assert(arena != NULL);
    assert(buffer != NULL);
    assert(size > 0);
    arena->base = (u8*)buffer;
    arena->size = size;
    arena->offset = 0;
}

void* mel_arena_alloc(Mel_Arena* arena, usize size, usize align)
{
    assert(arena != NULL);
    assert(size > 0);

    usize aligned_offset = align_forward(arena->offset, align);
    assert(aligned_offset + size <= arena->size);

    void* ptr = arena->base + aligned_offset;
    arena->offset = aligned_offset + size;
    memset(ptr, 0, size);
    return ptr;
}

void mel_arena_reset(Mel_Arena* arena)
{
    assert(arena != NULL);
    arena->offset = 0;
}

static void* arena_alloc_cb(void* ptr, usize size, u32 align,
                            const char* file, const char* func, u32 line,
                            void* user_data)
{
    MEL_UNUSED(file);
    MEL_UNUSED(func);
    MEL_UNUSED(line);
    Mel_Arena* arena = (Mel_Arena*)user_data;

    if (ptr == NULL && size > 0)
    {
        return mel_arena_alloc(arena, size, align > 0 ? align : 8);
    }

    if (ptr != NULL && size > 0)
    {
        void* new_ptr = mel_arena_alloc(arena, size, align > 0 ? align : 8);
        memcpy(new_ptr, ptr, size);
        return new_ptr;
    }

    return NULL;
}

Mel_Alloc mel_arena_allocator(Mel_Arena* arena)
{
    return (Mel_Alloc){
        .alloc_cb = arena_alloc_cb,
        .user_data = arena
    };
}

typedef struct Mel_Track_Entry Mel_Track_Entry;

struct Mel_Track_Entry
{
    Mel_Track_Entry* next;
    void* ptr;
    usize size;
};

static void* tracking_alloc_cb(void* ptr, usize size, u32 align,
                               const char* file, const char* func, u32 line,
                               void* user_data)
{
    Mel_Tracking_Allocator* t = (Mel_Tracking_Allocator*)user_data;

    void* result = t->backing->alloc_cb(ptr, size, align, file, func, line, t->backing->user_data);

    if (ptr == NULL && size > 0)
    {
        t->total_allocated += size;
        t->current_usage += size;
        t->alloc_count++;
        if (t->current_usage > t->peak_usage)
        {
            t->peak_usage = t->current_usage;
        }
    }
    else if (ptr != NULL && size > 0)
    {
        t->total_allocated += size;
        t->alloc_count++;
        if (t->current_usage > t->peak_usage)
        {
            t->peak_usage = t->current_usage;
        }
    }
    else if (ptr != NULL && size == 0)
    {
        t->free_count++;
    }

    return result;
}

void mel_tracking_init(Mel_Tracking_Allocator* t, const Mel_Alloc* backing)
{
    assert(t != NULL);
    assert(backing != NULL);
    t->backing = backing;
    t->total_allocated = 0;
    t->total_freed = 0;
    t->current_usage = 0;
    t->peak_usage = 0;
    t->alloc_count = 0;
    t->free_count = 0;
}

Mel_Alloc mel_tracking_allocator(Mel_Tracking_Allocator* t)
{
    return (Mel_Alloc){
        .alloc_cb = tracking_alloc_cb,
        .user_data = t
    };
}

void mel_tracking_report(Mel_Tracking_Allocator* t)
{
    assert(t != NULL);
    printf("=== Memory Tracking Report ===\n");
    printf("Total allocated: %zu bytes\n", t->total_allocated);
    printf("Total freed:     %zu bytes\n", t->total_freed);
    printf("Current usage:   %zu bytes\n", t->current_usage);
    printf("Peak usage:      %zu bytes\n", t->peak_usage);
    printf("Alloc count:     %llu\n", t->alloc_count);
    printf("Free count:      %llu\n", t->free_count);
    if (t->current_usage > 0)
    {
        printf("WARNING: %zu bytes still allocated (potential leak)\n", t->current_usage);
    }
    printf("==============================\n");
}
