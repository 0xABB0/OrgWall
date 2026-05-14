#include <allocator.heap/allocator.heap.h>
#include <allocator.guard/allocator.guard.h>
#include <allocator/allocator.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern Mel_Mem_Fail_Cb mel__get_fail_cb(void);

typedef struct {
    void* raw;
    usize size;
    u64 magic;
} Mel_Heap_Aligned_Header;

#define MEL__HEAP_ALIGNED_MAGIC 0x4D454C4845415041ull

typedef struct {
    atomic_uint init_state;
    Mel_Guard_Allocator allocator;
    Mel_Alloc iface;
} Mel_Heap_Guard_State;

static uintptr_t mel__heap_align_up_uintptr(uintptr_t value, usize align)
{
    return (value + align - 1u) & ~((uintptr_t)align - 1u);
}

static void* mel__heap_alloc_fail(const char* file, const char* func, u32 line, usize size)
{
    Mel_Mem_Fail_Cb fail_cb = mel__get_fail_cb();
    if (fail_cb)
        fail_cb(file, func, line, size);
    return NULL;
}

static void* mel__heap_aligned_alloc(usize size, u32 align,
                                     const char* file, const char* func, u32 line)
{
    usize req_align = align ? (usize)align : (usize)_Alignof(max_align_t);
    if (req_align < sizeof(void*))
        req_align = sizeof(void*);

    usize total = sizeof(Mel_Heap_Aligned_Header) + req_align + size;
    u8* raw = (u8*)malloc(total);
    if (!raw)
        return mel__heap_alloc_fail(file, func, line, size);

    uintptr_t user_addr = mel__heap_align_up_uintptr((uintptr_t)(raw + sizeof(Mel_Heap_Aligned_Header)), req_align);
    Mel_Heap_Aligned_Header* header = (Mel_Heap_Aligned_Header*)(user_addr - sizeof(*header));
    header->raw = raw;
    header->size = size;
    header->magic = MEL__HEAP_ALIGNED_MAGIC;
    return (void*)user_addr;
}

static void mel__heap_aligned_free(void* ptr)
{
    Mel_Heap_Aligned_Header* header = ((Mel_Heap_Aligned_Header*)ptr) - 1;
    assert(header->magic == MEL__HEAP_ALIGNED_MAGIC);
    header->magic = 0;
    free(header->raw);
}

static void* mel__heap_aligned_realloc(void* ptr, usize size, u32 align,
                                       const char* file, const char* func, u32 line)
{
    Mel_Heap_Aligned_Header* old_header = ((Mel_Heap_Aligned_Header*)ptr) - 1;
    assert(old_header->magic == MEL__HEAP_ALIGNED_MAGIC);

    void* new_ptr = mel__heap_aligned_alloc(size, align, file, func, line);
    if (!new_ptr)
        return NULL;

    memcpy(new_ptr, ptr, old_header->size < size ? old_header->size : size);
    mel__heap_aligned_free(ptr);
    return new_ptr;
}

static void* heap_alloc_cb(void* ptr, usize size, u32 align,
                           const char* file, const char* func, u32 line,
                           void* user_data)
{
    MEL_UNUSED(user_data);

    if (align > 0)
    {
        if (ptr == NULL && size > 0)
            return mel__heap_aligned_alloc(size, align, file, func, line);
        if (ptr != NULL && size > 0)
            return mel__heap_aligned_realloc(ptr, size, align, file, func, line);
        if (ptr != NULL && size == 0)
        {
            mel__heap_aligned_free(ptr);
            return NULL;
        }
        return NULL;
    }

    if (ptr == NULL && size > 0)
    {
        void* result = malloc(size);
        if (!result)
            return mel__heap_alloc_fail(file, func, line, size);
        return result;
    }

    if (ptr != NULL && size > 0)
    {
        void* result = realloc(ptr, size);
        if (!result)
            return mel__heap_alloc_fail(file, func, line, size);
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

#if MEL_MEMORY_DEBUG != MEL_MEMORY_DEBUG_NONE
static Mel_Heap_Guard_State s_heap_guard = {
    .init_state = 0,
};

static Mel_Guard_Allocator_Opt mel__heap_guard_opt(void)
{
#if MEL_MEMORY_DEBUG == MEL_MEMORY_DEBUG_LIGHT
    return (Mel_Guard_Allocator_Opt){
        .backing = &s_heap_alloc,
        .pre_guard_size = 32,
        .post_guard_size = 32,
        .quarantine_bytes = 64ull * 1024ull * 1024ull,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD |
                 MEL_GUARD_FLAG_CANARY_TAIL |
                 MEL_GUARD_FLAG_POISON_ALLOC |
                 MEL_GUARD_FLAG_POISON_FREE |
                 MEL_GUARD_FLAG_QUARANTINE |
                 MEL_GUARD_FLAG_CHECK_FREE,
    };
#elif MEL_MEMORY_DEBUG == MEL_MEMORY_DEBUG_HEAVY
    return (Mel_Guard_Allocator_Opt){
        .backing = &s_heap_alloc,
        .pre_guard_size = 32,
        .post_guard_size = 32,
        .quarantine_bytes = 128ull * 1024ull * 1024ull,
        .page_protect_min_size = 16ull * 1024ull,
        .protected_overhead_budget = 256ull * 1024ull * 1024ull,
        .sample_every = 64,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD |
                 MEL_GUARD_FLAG_CANARY_TAIL |
                 MEL_GUARD_FLAG_POISON_ALLOC |
                 MEL_GUARD_FLAG_POISON_FREE |
                 MEL_GUARD_FLAG_QUARANTINE |
                 MEL_GUARD_FLAG_PAGE_PROTECT |
                 MEL_GUARD_FLAG_CHECK_ALLOC |
                 MEL_GUARD_FLAG_CHECK_FREE,
    };
#else
    return (Mel_Guard_Allocator_Opt){
        .backing = &s_heap_alloc,
        .pre_guard_size = 64,
        .post_guard_size = 64,
        .quarantine_bytes = 256ull * 1024ull * 1024ull,
        .page_protect_min_size = 1,
        .protected_overhead_budget = 1024ull * 1024ull * 1024ull,
        .sample_every = 1,
        .flags = MEL_GUARD_FLAG_CANARY_HEAD |
                 MEL_GUARD_FLAG_CANARY_TAIL |
                 MEL_GUARD_FLAG_POISON_ALLOC |
                 MEL_GUARD_FLAG_POISON_FREE |
                 MEL_GUARD_FLAG_QUARANTINE |
                 MEL_GUARD_FLAG_PAGE_PROTECT |
                 MEL_GUARD_FLAG_CHECK_ALLOC |
                 MEL_GUARD_FLAG_CHECK_FREE,
    };
#endif
}

static void mel__heap_guard_init_once(void)
{
    u32 expected = 0;
    if (atomic_compare_exchange_strong_explicit(&s_heap_guard.init_state, &expected, 1,
            memory_order_acq_rel, memory_order_acquire))
    {
        mel_guard_init(&s_heap_guard.allocator, mel__heap_guard_opt());
        s_heap_guard.iface = mel_guard_allocator(&s_heap_guard.allocator);
        atomic_store_explicit(&s_heap_guard.init_state, 2, memory_order_release);
        return;
    }

    while (atomic_load_explicit(&s_heap_guard.init_state, memory_order_acquire) != 2) {}
}
#endif

const Mel_Alloc* mel_alloc_heap(void)
{
#if MEL_MEMORY_DEBUG == MEL_MEMORY_DEBUG_NONE
    return &s_heap_alloc;
#else
    mel__heap_guard_init_once();
    return &s_heap_guard.iface;
#endif
}

u32 mel_alloc_heap_memory_debug_level(void)
{
    return (u32)MEL_MEMORY_DEBUG;
}
