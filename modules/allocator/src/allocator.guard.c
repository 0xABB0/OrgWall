#include <allocator.guard/allocator.guard.h>
#include <allocator/allocator.h>
#include <allocator.vmem/allocator.vmem.h>

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern Mel_Mem_Fail_Cb mel__get_fail_cb(void);

typedef struct Mel_Guard_Header Mel_Guard_Header;

typedef struct {
    u64 magic;
    usize header_offset;
} Mel_Guard_Prefix;

struct Mel_Guard_Header {
    Mel_Guard_Header* prev;
    Mel_Guard_Header* next;
    usize requested_size;
    usize storage_size;
    usize region_size;
    usize usable_size;
    usize user_offset;
    u32 requested_align;
    u32 mode;
    const char* file;
    const char* func;
    u32 line;
    u32 _pad;
    u64 magic;
};

#define MEL__GUARD_HEADER_MAGIC     0x4D454C4755415244ull
#define MEL__GUARD_PREFIX_MAGIC     0x4D454C5052454649ull
#define MEL__GUARD_MODE_PROTECTED   (1u << 0)
#define MEL__GUARD_MODE_FREED       (1u << 1)

#define MEL__GUARD_CANARY_BYTE 0xFD
#define MEL__GUARD_ALLOC_BYTE  0xCD
#define MEL__GUARD_FREE_BYTE   0xDD

static uintptr_t mel__guard_align_up_uintptr(uintptr_t value, usize align)
{
    return (value + align - 1u) & ~((uintptr_t)align - 1u);
}

static uintptr_t mel__guard_align_down_uintptr(uintptr_t value, usize align)
{
    return value & ~((uintptr_t)align - 1u);
}

static usize mel__guard_default_align(u32 align)
{
    usize out = align ? (usize)align : (usize)_Alignof(max_align_t);
    if (out < sizeof(void*)) out = sizeof(void*);
    return out;
}

static void mel__guard_lock(Mel_Guard_Allocator* g)
{
    atomic_uint* lock = (atomic_uint*)&g->lock;
    while (atomic_exchange_explicit(lock, 1u, memory_order_acquire) != 0u) {}
}

static void mel__guard_unlock(Mel_Guard_Allocator* g)
{
    atomic_uint* lock = (atomic_uint*)&g->lock;
    atomic_store_explicit(lock, 0u, memory_order_release);
}

static void mel__guard_fail(Mel_Guard_Header* h, const char* why,
                            const char* file, const char* func, u32 line)
{
    fprintf(stderr,
        "guard allocator corruption: %s\n"
        "  alloc site: %s:%u (%s)\n"
        "  check site: %s:%u (%s)\n"
        "  size: %zu\n"
        "  mode: %s%s\n",
        why,
        h && h->file ? h->file : "<unknown>",
        h ? h->line : 0,
        h && h->func ? h->func : "<unknown>",
        file ? file : "<unknown>",
        line,
        func ? func : "<unknown>",
        h ? h->requested_size : 0,
        (h && (h->mode & MEL__GUARD_MODE_PROTECTED)) ? "protected" : "normal",
        (h && (h->mode & MEL__GUARD_MODE_FREED)) ? ", freed" : "");
    assert(!"guard allocator corruption");
}

static Mel_Guard_Header* mel__guard_header_from_user(void* user_ptr,
                                                     const char* file, const char* func, u32 line)
{
    Mel_Guard_Prefix* prefix = (Mel_Guard_Prefix*)((u8*)user_ptr - sizeof(Mel_Guard_Prefix));
    if (prefix->magic != MEL__GUARD_PREFIX_MAGIC)
    {
        mel__guard_fail(NULL, "prefix magic corrupted", file, func, line);
    }

    Mel_Guard_Header* h = (Mel_Guard_Header*)((u8*)user_ptr - prefix->header_offset);
    if (h->magic != MEL__GUARD_HEADER_MAGIC)
    {
        mel__guard_fail(h, "header magic corrupted", file, func, line);
    }
    if (h->mode & MEL__GUARD_MODE_FREED)
    {
        mel__guard_fail(h, "pointer already freed", file, func, line);
    }

    return h;
}

static void* mel__guard_user_ptr(Mel_Guard_Header* h)
{
    return (u8*)h + h->user_offset;
}

static void mel__guard_check_bytes(Mel_Guard_Header* h, const u8* ptr, usize size, u8 value,
                                   const char* why,
                                   const char* file, const char* func, u32 line)
{
    for (usize i = 0; i < size; ++i)
    {
        if (ptr[i] != value)
        {
            mel__guard_fail(h, why, file, func, line);
        }
    }
}

static void mel__guard_validate_user_locked(Mel_Guard_Allocator* g, Mel_Guard_Header* h,
                                            void* user_ptr,
                                            const char* file, const char* func, u32 line)
{
    if (h->magic != MEL__GUARD_HEADER_MAGIC)
    {
        mel__guard_fail(h, "header magic corrupted", file, func, line);
    }

    Mel_Guard_Prefix* prefix = (Mel_Guard_Prefix*)((u8*)user_ptr - sizeof(*prefix));
    if (prefix->magic != MEL__GUARD_PREFIX_MAGIC)
    {
        mel__guard_fail(h, "prefix magic corrupted", file, func, line);
    }
    if (prefix->header_offset != h->user_offset)
    {
        mel__guard_fail(h, "prefix offset corrupted", file, func, line);
    }

    if (g->flags & MEL_GUARD_FLAG_CANARY_HEAD)
    {
        u8* head_begin = (u8*)h + sizeof(*h);
        u8* head_end = (u8*)prefix;
        mel__guard_check_bytes(h, head_begin, (usize)(head_end - head_begin),
                               MEL__GUARD_CANARY_BYTE, "head guard corrupted",
                               file, func, line);
    }

    if ((g->flags & MEL_GUARD_FLAG_CANARY_TAIL) && !(h->mode & MEL__GUARD_MODE_PROTECTED))
    {
        u8* tail = (u8*)user_ptr + h->requested_size;
        mel__guard_check_bytes(h, tail, g->post_guard_size,
                               MEL__GUARD_CANARY_BYTE, "tail guard corrupted",
                               file, func, line);
    }
}

static void mel__guard_list_push_tail(Mel_Guard_Allocator* g, Mel_Guard_Header* h)
{
    h->prev = g->quarantine_tail;
    h->next = NULL;
    if (g->quarantine_tail)
        g->quarantine_tail->next = h;
    else
        g->quarantine_head = h;
    g->quarantine_tail = h;
}

static Mel_Guard_Header* mel__guard_list_pop_head(Mel_Guard_Allocator* g)
{
    Mel_Guard_Header* h = g->quarantine_head;
    if (!h)
        return NULL;

    g->quarantine_head = h->next;
    if (g->quarantine_head)
        g->quarantine_head->prev = NULL;
    else
        g->quarantine_tail = NULL;

    h->prev = NULL;
    h->next = NULL;
    return h;
}

static void mel__guard_check_quarantine_locked(Mel_Guard_Allocator* g,
                                               const char* file, const char* func, u32 line)
{
    for (Mel_Guard_Header* h = g->quarantine_head; h != NULL; h = h->next)
    {
        if (h->mode & MEL__GUARD_MODE_PROTECTED)
            continue;
        mel__guard_validate_user_locked(g, h, mel__guard_user_ptr(h), file, func, line);
    }
}

static void mel__guard_release_header(Mel_Guard_Allocator* g, Mel_Guard_Header* h)
{
    if (h->mode & MEL__GUARD_MODE_PROTECTED)
    {
        usize region_size = h->region_size;
        mel_vmem_release((void*)h, region_size);
        g->protected_bytes -= region_size;
        g->protected_allocs -= 1;
    }
    else
    {
        g->backing->alloc_cb(h, 0, 0, h->file, h->func, h->line, g->backing->user_data);
    }
}

static void mel__guard_evict_quarantine_locked(Mel_Guard_Allocator* g,
                                               const char* file, const char* func, u32 line)
{
    while (g->quarantined_bytes > g->quarantine_bytes)
    {
        Mel_Guard_Header* h = mel__guard_list_pop_head(g);
        if (!h)
            break;

        if (!(h->mode & MEL__GUARD_MODE_PROTECTED))
            mel__guard_validate_user_locked(g, h, mel__guard_user_ptr(h), file, func, line);

        g->quarantined_bytes -= h->requested_size;
        g->quarantined_allocs -= 1;
        mel__guard_release_header(g, h);
    }
}

static void* mel__guard_alloc_normal_locked(Mel_Guard_Allocator* g, usize size, u32 align,
                                            const char* file, const char* func, u32 line)
{
    usize req_align = mel__guard_default_align(align);
    usize total = sizeof(Mel_Guard_Header) + g->pre_guard_size + sizeof(Mel_Guard_Prefix) + req_align + size + g->post_guard_size;
    u8* raw = (u8*)g->backing->alloc_cb(NULL, total, 0, file, func, line, g->backing->user_data);
    if (!raw)
        return NULL;

    Mel_Guard_Header* h = (Mel_Guard_Header*)raw;
    uintptr_t user_addr = mel__guard_align_up_uintptr((uintptr_t)(raw + sizeof(*h) + g->pre_guard_size + sizeof(Mel_Guard_Prefix)), req_align);
    u8* user = (u8*)user_addr;
    Mel_Guard_Prefix* prefix = (Mel_Guard_Prefix*)(user - sizeof(*prefix));

    h->prev = NULL;
    h->next = NULL;
    h->requested_size = size;
    h->storage_size = total;
    h->region_size = 0;
    h->usable_size = 0;
    h->user_offset = (usize)(user - raw);
    h->requested_align = align;
    h->mode = 0;
    h->file = file;
    h->func = func;
    h->line = line;
    h->magic = MEL__GUARD_HEADER_MAGIC;

    prefix->magic = MEL__GUARD_PREFIX_MAGIC;
    prefix->header_offset = h->user_offset;

    if (g->flags & MEL_GUARD_FLAG_CANARY_HEAD)
        memset(raw + sizeof(*h), MEL__GUARD_CANARY_BYTE, (usize)((u8*)prefix - (raw + sizeof(*h))));
    if (g->flags & MEL_GUARD_FLAG_CANARY_TAIL)
        memset(user + size, MEL__GUARD_CANARY_BYTE, g->post_guard_size);
    if (g->flags & MEL_GUARD_FLAG_POISON_ALLOC)
        memset(user, MEL__GUARD_ALLOC_BYTE, size);

    g->live_bytes += size;
    g->live_allocs += 1;
    return user;
}

static void* mel__guard_alloc_protected_locked(Mel_Guard_Allocator* g, usize size, u32 align,
                                               const char* file, const char* func, u32 line)
{
    usize req_align = mel__guard_default_align(align);
    usize usable_min = sizeof(Mel_Guard_Header) + g->pre_guard_size + sizeof(Mel_Guard_Prefix) + req_align + size;
    usize usable_size = mel_vmem_align_to_page(usable_min);
    usize region_size = usable_size + mel_vmem_page_size();

    if (g->protected_overhead_budget > 0 && (g->protected_bytes + region_size) > g->protected_overhead_budget)
        return mel__guard_alloc_normal_locked(g, size, align, file, func, line);

    u8* base = (u8*)mel_vmem_reserve(region_size);
    if (!base)
        return mel__guard_alloc_normal_locked(g, size, align, file, func, line);
    if (!mel_vmem_commit(base, usable_size))
    {
        mel_vmem_release(base, region_size);
        return mel__guard_alloc_normal_locked(g, size, align, file, func, line);
    }

    Mel_Guard_Header* h = (Mel_Guard_Header*)base;
    uintptr_t usable_end = (uintptr_t)(base + usable_size);
    uintptr_t user_addr = mel__guard_align_down_uintptr(usable_end - size, req_align);
    Mel_Guard_Prefix* prefix = (Mel_Guard_Prefix*)(user_addr - sizeof(*prefix));
    u8* user = (u8*)user_addr;

    if ((u8*)prefix < base + sizeof(*h) + g->pre_guard_size)
    {
        mel_vmem_decommit(base, usable_size);
        mel_vmem_release(base, region_size);
        return mel__guard_alloc_normal_locked(g, size, align, file, func, line);
    }

    h->prev = NULL;
    h->next = NULL;
    h->requested_size = size;
    h->storage_size = usable_size;
    h->region_size = region_size;
    h->usable_size = usable_size;
    h->user_offset = (usize)(user - base);
    h->requested_align = align;
    h->mode = MEL__GUARD_MODE_PROTECTED;
    h->file = file;
    h->func = func;
    h->line = line;
    h->magic = MEL__GUARD_HEADER_MAGIC;

    prefix->magic = MEL__GUARD_PREFIX_MAGIC;
    prefix->header_offset = h->user_offset;

    if (g->flags & MEL_GUARD_FLAG_CANARY_HEAD)
        memset(base + sizeof(*h), MEL__GUARD_CANARY_BYTE, (usize)((u8*)prefix - (base + sizeof(*h))));
    if (g->flags & MEL_GUARD_FLAG_POISON_ALLOC)
        memset(user, MEL__GUARD_ALLOC_BYTE, size);

    g->live_bytes += size;
    g->live_allocs += 1;
    g->protected_bytes += region_size;
    g->protected_allocs += 1;
    return user;
}

static bool mel__guard_should_protect_locked(Mel_Guard_Allocator* g, usize size)
{
    if ((g->flags & MEL_GUARD_FLAG_PAGE_PROTECT) == 0)
        return false;
    if (g->page_protect_min_size > 0 && size >= g->page_protect_min_size)
        return true;
    if (g->sample_every > 0 && (g->alloc_index % g->sample_every) == 0)
        return true;
    return false;
}

static void* mel__guard_alloc_locked(Mel_Guard_Allocator* g, usize size, u32 align,
                                     const char* file, const char* func, u32 line)
{
    g->alloc_index += 1;
    if (g->flags & MEL_GUARD_FLAG_CHECK_ALLOC)
        mel__guard_check_quarantine_locked(g, file, func, line);

    if (mel__guard_should_protect_locked(g, size))
        return mel__guard_alloc_protected_locked(g, size, align, file, func, line);
    return mel__guard_alloc_normal_locked(g, size, align, file, func, line);
}

static void mel__guard_free_locked(Mel_Guard_Allocator* g, void* ptr,
                                   const char* file, const char* func, u32 line)
{
    Mel_Guard_Header* h = mel__guard_header_from_user(ptr, file, func, line);
    mel__guard_validate_user_locked(g, h, ptr, file, func, line);

    if (g->flags & MEL_GUARD_FLAG_POISON_FREE)
        memset(ptr, MEL__GUARD_FREE_BYTE, h->requested_size);

    h->mode |= MEL__GUARD_MODE_FREED;
    g->live_bytes -= h->requested_size;
    g->live_allocs -= 1;

    if (g->flags & MEL_GUARD_FLAG_QUARANTINE)
    {
        mel__guard_list_push_tail(g, h);
        g->quarantined_bytes += h->requested_size;
        g->quarantined_allocs += 1;
        mel__guard_evict_quarantine_locked(g, file, func, line);
    }
    else
    {
        mel__guard_release_header(g, h);
    }
}

static void* guard_alloc_cb(void* ptr, usize size, u32 align,
                            const char* file, const char* func, u32 line,
                            void* user_data)
{
    Mel_Guard_Allocator* g = (Mel_Guard_Allocator*)user_data;
    assert(g != NULL);
    assert(g->initialized);

    mel__guard_lock(g);

    void* result = NULL;
    if (ptr == NULL && size > 0)
    {
        result = mel__guard_alloc_locked(g, size, align, file, func, line);
    }
    else if (ptr != NULL && size > 0)
    {
        Mel_Guard_Header* old_h = mel__guard_header_from_user(ptr, file, func, line);
        mel__guard_validate_user_locked(g, old_h, ptr, file, func, line);
        result = mel__guard_alloc_locked(g, size, align, file, func, line);
        if (result)
        {
            memcpy(result, ptr, old_h->requested_size < size ? old_h->requested_size : size);
            mel__guard_free_locked(g, ptr, file, func, line);
        }
    }
    else if (ptr != NULL && size == 0)
    {
        mel__guard_free_locked(g, ptr, file, func, line);
    }

    if (g->flags & MEL_GUARD_FLAG_CHECK_FREE)
        mel__guard_check_quarantine_locked(g, file, func, line);

    mel__guard_unlock(g);
    return result;
}

void mel_guard_init(Mel_Guard_Allocator* g, Mel_Guard_Allocator_Opt opt)
{
    assert(g != NULL);
    assert(opt.backing != NULL);
    memset(g, 0, sizeof(*g));
    g->backing = opt.backing;
    g->pre_guard_size = opt.pre_guard_size;
    g->post_guard_size = opt.post_guard_size;
    g->quarantine_bytes = opt.quarantine_bytes;
    g->page_protect_min_size = opt.page_protect_min_size;
    g->protected_overhead_budget = opt.protected_overhead_budget;
    g->sample_every = opt.sample_every;
    g->flags = opt.flags;
    g->initialized = true;
}

void mel_guard_shutdown(Mel_Guard_Allocator* g)
{
    assert(g != NULL);
    mel__guard_lock(g);
    while (g->quarantine_head)
    {
        Mel_Guard_Header* h = mel__guard_list_pop_head(g);
        g->quarantined_bytes -= h->requested_size;
        g->quarantined_allocs -= 1;
        mel__guard_release_header(g, h);
    }
    assert(g->live_allocs == 0);
    assert(g->live_bytes == 0);
    g->initialized = false;
    mel__guard_unlock(g);
}

Mel_Alloc mel_guard_allocator(Mel_Guard_Allocator* g)
{
    assert(g != NULL);
    return (Mel_Alloc){
        .alloc_cb = guard_alloc_cb,
        .user_data = g,
    };
}

void mel_guard_check(Mel_Guard_Allocator* g)
{
    assert(g != NULL);
    mel__guard_lock(g);
    mel__guard_check_quarantine_locked(g, NULL, NULL, 0);
    mel__guard_unlock(g);
}

Mel_Guard_Allocator_Stats mel_guard_stats(Mel_Guard_Allocator* g)
{
    assert(g != NULL);
    mel__guard_lock(g);
    Mel_Guard_Allocator_Stats stats = {
        .live_bytes = g->live_bytes,
        .live_allocs = g->live_allocs,
        .quarantined_bytes = g->quarantined_bytes,
        .quarantined_allocs = g->quarantined_allocs,
        .protected_bytes = g->protected_bytes,
        .protected_allocs = g->protected_allocs,
        .alloc_index = g->alloc_index,
    };
    mel__guard_unlock(g);
    return stats;
}
