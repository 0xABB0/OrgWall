#include <allocator/tracking.h>
#include <allocator/allocator.h>

#include <core/compiler.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ANDROID__)
#if __ANDROID_API__ >= 33
#include <execinfo.h>
#define MEL__TRACK_HAS_BACKTRACE 1
#else
#define MEL__TRACK_HAS_BACKTRACE 0
#endif
#elif defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#define MEL__TRACK_HAS_BACKTRACE 1
#elif defined(_WIN32)
#include <windows.h>
#define MEL__TRACK_HAS_BACKTRACE 1
#else
#define MEL__TRACK_HAS_BACKTRACE 0
#endif

typedef struct Mel_Track_Site   Mel_Track_Site;
typedef struct Mel_Track_Tag    Mel_Track_Tag;
typedef struct Mel_Track_Header Mel_Track_Header;

#define MEL__TRACK_SLOT_EMPTY    0u
#define MEL__TRACK_SLOT_OCCUPIED 1u

struct Mel_Track_Slot
{
    u64   key;
    void* val;
    u8    state;
};

struct Mel_Track_Site
{
    const char* file;
    u32         line;
    usize       live_bytes;
    usize       live_allocs;
    usize       peak_bytes;
    usize       total_bytes;
    usize       alloc_count;
    usize       free_count;
};

struct Mel_Track_Tag
{
    const char* tag;
    usize       live_bytes;
    usize       live_allocs;
    usize       peak_bytes;
    usize       total_bytes;
    usize       alloc_count;
    usize       free_count;
};

struct Mel_Track_Header
{
    void*           user_ptr;
    usize           size;
    const char*     file;
    const char*     func;
    u32             line;
    const char*     tag;
    Mel_Track_Site* site;
    Mel_Track_Tag*  tag_rec;
    u64             seq;
    usize           frame_count;
};

static void mel__track_lock(Mel_Track_Allocator* t)
{
    atomic_uint* lock = (atomic_uint*)&t->lock;
    while (atomic_exchange_explicit(lock, 1u, memory_order_acquire) != 0u)
    {
    }
}

static void mel__track_unlock(Mel_Track_Allocator* t)
{
    atomic_uint* lock = (atomic_uint*)&t->lock;
    atomic_store_explicit(lock, 0u, memory_order_release);
}

static u64 mel__track_mix(u64 x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

static u64 mel__track_hash_str(const char* s)
{
    u64 h = 0xcbf29ce484222325ull;
    if (s)
    {
        for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
        {
            h ^= (u64)*p;
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

static u64 mel__track_site_key(const char* file, u32 line) { return mel__track_hash_str(file) ^ ((u64)line * 0x9e3779b97f4a7c15ull); }

static struct Mel_Track_Slot* mel__track_slots_alloc(const Mel_Alloc* meta, usize cap)
{
    usize                  bytes = cap * sizeof(struct Mel_Track_Slot);
    struct Mel_Track_Slot* s = (struct Mel_Track_Slot*)meta->alloc_cb(NULL, bytes, 0, __FILE__, __func__, __LINE__, meta->user_data);
    assert(s);
    memset(s, 0, bytes);
    return s;
}

static void mel__track_map_grow(Mel_Track_Map* m, const Mel_Alloc* meta, usize min_cap)
{
    usize new_cap = m->capacity ? m->capacity * 2 : 8;
    while (new_cap < min_cap)
        new_cap *= 2;

    struct Mel_Track_Slot* ns = mel__track_slots_alloc(meta, new_cap);
    u64                    mask = (u64)new_cap - 1;
    for (usize i = 0; i < m->capacity; ++i)
    {
        if (m->slots[i].state != MEL__TRACK_SLOT_OCCUPIED)
            continue;
        u64 j = mel__track_mix(m->slots[i].key) & mask;
        while (ns[j].state == MEL__TRACK_SLOT_OCCUPIED)
            j = (j + 1) & mask;
        ns[j] = m->slots[i];
    }
    if (m->slots)
        meta->alloc_cb(m->slots, 0, 0, __FILE__, __func__, __LINE__, meta->user_data);
    m->slots = ns;
    m->capacity = new_cap;
}

static void mel__track_map_put(Mel_Track_Map* m, const Mel_Alloc* meta, u64 key, void* val)
{
    if (m->capacity == 0 || (m->count + 1) * 4 >= m->capacity * 3)
        mel__track_map_grow(m, meta, m->count + 1);

    u64 mask = (u64)m->capacity - 1;
    u64 i = mel__track_mix(key) & mask;
    while (m->slots[i].state == MEL__TRACK_SLOT_OCCUPIED)
    {
        if (m->slots[i].key == key)
        {
            m->slots[i].val = val;
            return;
        }
        i = (i + 1) & mask;
    }
    m->slots[i].key = key;
    m->slots[i].val = val;
    m->slots[i].state = MEL__TRACK_SLOT_OCCUPIED;
    m->count++;
}

static void* mel__track_map_get(Mel_Track_Map* m, u64 key)
{
    if (m->capacity == 0)
        return NULL;
    u64 mask = (u64)m->capacity - 1;
    u64 i = mel__track_mix(key) & mask;
    while (m->slots[i].state == MEL__TRACK_SLOT_OCCUPIED)
    {
        if (m->slots[i].key == key)
            return m->slots[i].val;
        i = (i + 1) & mask;
    }
    return NULL;
}

static void* mel__track_map_take(Mel_Track_Map* m, u64 key)
{
    if (m->capacity == 0)
        return NULL;
    u64 mask = (u64)m->capacity - 1;
    u64 i = mel__track_mix(key) & mask;
    while (m->slots[i].state == MEL__TRACK_SLOT_OCCUPIED && m->slots[i].key != key)
        i = (i + 1) & mask;
    if (m->slots[i].state != MEL__TRACK_SLOT_OCCUPIED)
        return NULL;

    void* val = m->slots[i].val;
    m->slots[i].state = MEL__TRACK_SLOT_EMPTY;
    m->count--;

    u64 j = i;
    for (;;)
    {
        j = (j + 1) & mask;
        if (m->slots[j].state != MEL__TRACK_SLOT_OCCUPIED)
            break;
        u64  home = mel__track_mix(m->slots[j].key) & mask;
        bool in_range = (i < j) ? (i < home && home <= j) : (i < home || home <= j);
        if (!in_range)
        {
            m->slots[i] = m->slots[j];
            m->slots[j].state = MEL__TRACK_SLOT_EMPTY;
            i = j;
        }
    }
    return val;
}

typedef struct
{
    const char** items;
    usize        count;
    usize        cap;
} Mel__Track_Scope;

static MEL_THREAD_LOCAL Mel__Track_Scope s_track_scope;

void mel_track_scope_push(const char* tag)
{
    Mel__Track_Scope* s = &s_track_scope;
    if (s->count == s->cap)
    {
        usize        ncap = s->cap ? s->cap * 2 : 8;
        const char** ni = (const char**)realloc(s->items, ncap * sizeof(const char*));
        assert(ni);
        s->items = ni;
        s->cap = ncap;
    }
    s->items[s->count++] = tag;
}

void mel_track_scope_pop(void)
{
    Mel__Track_Scope* s = &s_track_scope;
    if (s->count == 0)
        return;
    s->count--;
    if (s->count == 0)
    {
        free(s->items);
        s->items = NULL;
        s->cap = 0;
    }
}

static const char* mel__track_scope_top(void)
{
    Mel__Track_Scope* s = &s_track_scope;
    return s->count ? s->items[s->count - 1] : NULL;
}

static Mel_Track_Site* mel__track_site(Mel_Track_Allocator* t, const char* file, u32 line)
{
    u64             key = mel__track_site_key(file, line);
    Mel_Track_Site* s = (Mel_Track_Site*)mel__track_map_get(&t->sites, key);
    if (s)
        return s;
    s = (Mel_Track_Site*)t->meta->alloc_cb(NULL, sizeof(*s), 0, __FILE__, __func__, __LINE__, t->meta->user_data);
    assert(s);
    memset(s, 0, sizeof(*s));
    s->file = file;
    s->line = line;
    mel__track_map_put(&t->sites, t->meta, key, s);
    return s;
}

static Mel_Track_Tag* mel__track_tag(Mel_Track_Allocator* t, const char* tag)
{
    u64            key = mel__track_hash_str(tag);
    Mel_Track_Tag* r = (Mel_Track_Tag*)mel__track_map_get(&t->tags, key);
    if (r)
        return r;
    r = (Mel_Track_Tag*)t->meta->alloc_cb(NULL, sizeof(*r), 0, __FILE__, __func__, __LINE__, t->meta->user_data);
    assert(r);
    memset(r, 0, sizeof(*r));
    r->tag = tag;
    mel__track_map_put(&t->tags, t->meta, key, r);
    return r;
}

static Mel_Track_Header* mel__track_header_make(Mel_Track_Allocator* t, void* user, usize size, const char* file, const char* func, u32 line)
{
    usize             frames_bytes = (t->flags & MEL_TRACK_FLAG_BACKTRACE) ? (usize)t->backtrace_depth * sizeof(void*) : 0;
    Mel_Track_Header* h = (Mel_Track_Header*)t->meta->alloc_cb(NULL, sizeof(*h) + frames_bytes, 0, file, func, line, t->meta->user_data);
    assert(h);
    h->user_ptr = user;
    h->size = size;
    h->file = file;
    h->func = func;
    h->line = line;
    h->tag = mel__track_scope_top();
    h->site = (t->flags & MEL_TRACK_FLAG_AGGREGATE_SITE) ? mel__track_site(t, file, line) : NULL;
    h->tag_rec = ((t->flags & MEL_TRACK_FLAG_AGGREGATE_TAG) && h->tag) ? mel__track_tag(t, h->tag) : NULL;
    h->seq = t->seq++;
    h->frame_count = 0;
#if MEL__TRACK_HAS_BACKTRACE
    if (t->flags & MEL_TRACK_FLAG_BACKTRACE)
    {
        void** frames = (void**)(h + 1);
#if defined(_WIN32)
        USHORT n = CaptureStackBackTrace(0, (DWORD)t->backtrace_depth, frames, NULL);
        h->frame_count = (usize)n;
#else
        int n = backtrace(frames, (int)t->backtrace_depth);
        h->frame_count = n > 0 ? (usize)n : 0;
#endif
    }
#endif
    return h;
}

static void mel__track_account_alloc(Mel_Track_Allocator* t, Mel_Track_Header* h)
{
    t->live_bytes += h->size;
    t->live_allocs += 1;
    if (t->live_bytes > t->peak_bytes)
        t->peak_bytes = t->live_bytes;
    if (t->live_allocs > t->peak_allocs)
        t->peak_allocs = t->live_allocs;

    if (h->site)
    {
        h->site->live_bytes += h->size;
        h->site->live_allocs += 1;
        h->site->total_bytes += h->size;
        h->site->alloc_count += 1;
        if (h->site->live_bytes > h->site->peak_bytes)
            h->site->peak_bytes = h->site->live_bytes;
    }
    if (h->tag_rec)
    {
        h->tag_rec->live_bytes += h->size;
        h->tag_rec->live_allocs += 1;
        h->tag_rec->total_bytes += h->size;
        h->tag_rec->alloc_count += 1;
        if (h->tag_rec->live_bytes > h->tag_rec->peak_bytes)
            h->tag_rec->peak_bytes = h->tag_rec->live_bytes;
    }
}

static void mel__track_account_free(Mel_Track_Allocator* t, Mel_Track_Header* h)
{
    t->live_bytes -= h->size;
    t->live_allocs -= 1;
    if (h->site)
    {
        h->site->live_bytes -= h->size;
        h->site->live_allocs -= 1;
        h->site->free_count += 1;
    }
    if (h->tag_rec)
    {
        h->tag_rec->live_bytes -= h->size;
        h->tag_rec->live_allocs -= 1;
        h->tag_rec->free_count += 1;
    }
}

static void* mel__track_alloc_locked(Mel_Track_Allocator* t, usize size, u32 align, const char* file, const char* func, u32 line)
{
    void* user = t->backing->alloc_cb(NULL, size, align, file, func, line, t->backing->user_data);
    if (!user)
        return NULL;
    Mel_Track_Header* h = mel__track_header_make(t, user, size, file, func, line);
    mel__track_map_put(&t->registry, t->meta, (u64)(uintptr_t)user, h);
    mel__track_account_alloc(t, h);
    t->total_alloc_bytes += size;
    t->total_alloc_count += 1;
    return user;
}

static void mel__track_free_locked(Mel_Track_Allocator* t, void* ptr, u32 align, const char* file, const char* func, u32 line)
{
    Mel_Track_Header* h = (Mel_Track_Header*)mel__track_map_take(&t->registry, (u64)(uintptr_t)ptr);
    assert(h && "tracking: free of untracked pointer");
    mel__track_account_free(t, h);
    t->total_free_count += 1;
    t->backing->alloc_cb(ptr, 0, align, file, func, line, t->backing->user_data);
    t->meta->alloc_cb(h, 0, 0, file, func, line, t->meta->user_data);
}

static void* mel__track_realloc_locked(Mel_Track_Allocator* t, void* ptr, usize size, u32 align, const char* file, const char* func, u32 line)
{
    Mel_Track_Header* old = (Mel_Track_Header*)mel__track_map_take(&t->registry, (u64)(uintptr_t)ptr);
    assert(old && "tracking: realloc of untracked pointer");

    void* user = t->backing->alloc_cb(ptr, size, align, file, func, line, t->backing->user_data);
    if (!user)
    {
        mel__track_map_put(&t->registry, t->meta, (u64)(uintptr_t)ptr, old);
        return NULL;
    }

    mel__track_account_free(t, old);
    t->meta->alloc_cb(old, 0, 0, file, func, line, t->meta->user_data);

    Mel_Track_Header* h = mel__track_header_make(t, user, size, file, func, line);
    mel__track_map_put(&t->registry, t->meta, (u64)(uintptr_t)user, h);
    mel__track_account_alloc(t, h);
    t->total_realloc_count += 1;
    return user;
}

static void* mel__track_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user_data)
{
    Mel_Track_Allocator* t = (Mel_Track_Allocator*)user_data;
    assert(t != NULL);
    assert(t->initialized);

    mel__track_lock(t);
    void* result = NULL;
    if (ptr == NULL && size > 0)
        result = mel__track_alloc_locked(t, size, align, file, func, line);
    else if (ptr != NULL && size > 0)
        result = mel__track_realloc_locked(t, ptr, size, align, file, func, line);
    else if (ptr != NULL && size == 0)
        mel__track_free_locked(t, ptr, align, file, func, line);
    mel__track_unlock(t);
    return result;
}

void mel_track_init(Mel_Track_Allocator* t, Mel_Track_Allocator_Opt opt)
{
    assert(t != NULL);
    assert(opt.backing != NULL);
    memset(t, 0, sizeof(*t));
    t->backing = opt.backing;
    t->meta = opt.meta ? opt.meta : opt.backing;
    t->flags = opt.flags;
    t->backtrace_depth = opt.backtrace_depth ? opt.backtrace_depth : MEL_TRACK_BACKTRACE_DEPTH_DEFAULT;
    t->initialized = true;
}

void mel_track_shutdown(Mel_Track_Allocator* t)
{
    assert(t != NULL);
    mel__track_lock(t);
    assert(t->live_allocs == 0 && "tracking: shutdown with live allocations (leaks); dump before shutdown");
    assert(t->live_bytes == 0);

    for (usize i = 0; i < t->sites.capacity; ++i)
        if (t->sites.slots[i].state == MEL__TRACK_SLOT_OCCUPIED)
            t->meta->alloc_cb(t->sites.slots[i].val, 0, 0, __FILE__, __func__, __LINE__, t->meta->user_data);
    for (usize i = 0; i < t->tags.capacity; ++i)
        if (t->tags.slots[i].state == MEL__TRACK_SLOT_OCCUPIED)
            t->meta->alloc_cb(t->tags.slots[i].val, 0, 0, __FILE__, __func__, __LINE__, t->meta->user_data);

    if (t->registry.slots)
        t->meta->alloc_cb(t->registry.slots, 0, 0, __FILE__, __func__, __LINE__, t->meta->user_data);
    if (t->sites.slots)
        t->meta->alloc_cb(t->sites.slots, 0, 0, __FILE__, __func__, __LINE__, t->meta->user_data);
    if (t->tags.slots)
        t->meta->alloc_cb(t->tags.slots, 0, 0, __FILE__, __func__, __LINE__, t->meta->user_data);

    t->initialized = false;
    mel__track_unlock(t);
}

Mel_Alloc mel_track_allocator(Mel_Track_Allocator* t)
{
    assert(t != NULL);
    return (Mel_Alloc){
        .alloc_cb = mel__track_cb,
        .user_data = t,
    };
}

Mel_Track_Allocator_Stats mel_track_stats(Mel_Track_Allocator* t)
{
    assert(t != NULL);
    mel__track_lock(t);
    Mel_Track_Allocator_Stats stats = {
        .live_bytes = t->live_bytes,
        .live_allocs = t->live_allocs,
        .peak_bytes = t->peak_bytes,
        .peak_allocs = t->peak_allocs,
        .total_alloc_bytes = t->total_alloc_bytes,
        .total_alloc_count = t->total_alloc_count,
        .total_free_count = t->total_free_count,
        .total_realloc_count = t->total_realloc_count,
    };
    mel__track_unlock(t);
    return stats;
}

void mel_track_dump_live(Mel_Track_Allocator* t, Mel_Track_Report_Cb cb, void* user_data)
{
    assert(t != NULL);
    assert(cb != NULL);
    mel__track_lock(t);
    for (usize i = 0; i < t->registry.capacity; ++i)
    {
        if (t->registry.slots[i].state != MEL__TRACK_SLOT_OCCUPIED)
            continue;
        Mel_Track_Header* h = (Mel_Track_Header*)t->registry.slots[i].val;
        Mel_Track_Record  rec = {
             .file = h->file,
             .func = h->func,
             .line = h->line,
             .size = h->size,
             .tag = h->tag,
             .seq = h->seq,
             .frames = h->frame_count ? (void* const*)(h + 1) : NULL,
             .frame_count = h->frame_count,
        };
        cb(&rec, user_data);
    }
    mel__track_unlock(t);
}

void mel_track_dump_sites(Mel_Track_Allocator* t, Mel_Track_Bucket_Cb cb, void* user_data)
{
    assert(t != NULL);
    assert(cb != NULL);
    mel__track_lock(t);
    for (usize i = 0; i < t->sites.capacity; ++i)
    {
        if (t->sites.slots[i].state != MEL__TRACK_SLOT_OCCUPIED)
            continue;
        Mel_Track_Site*  s = (Mel_Track_Site*)t->sites.slots[i].val;
        Mel_Track_Bucket bucket = {
            .key = s->file,
            .line = s->line,
            .live_bytes = s->live_bytes,
            .live_allocs = s->live_allocs,
            .peak_bytes = s->peak_bytes,
            .total_bytes = s->total_bytes,
            .alloc_count = s->alloc_count,
            .free_count = s->free_count,
        };
        cb(&bucket, user_data);
    }
    mel__track_unlock(t);
}

void mel_track_dump_tags(Mel_Track_Allocator* t, Mel_Track_Bucket_Cb cb, void* user_data)
{
    assert(t != NULL);
    assert(cb != NULL);
    mel__track_lock(t);
    for (usize i = 0; i < t->tags.capacity; ++i)
    {
        if (t->tags.slots[i].state != MEL__TRACK_SLOT_OCCUPIED)
            continue;
        Mel_Track_Tag*   r = (Mel_Track_Tag*)t->tags.slots[i].val;
        Mel_Track_Bucket bucket = {
            .key = r->tag,
            .line = 0,
            .live_bytes = r->live_bytes,
            .live_allocs = r->live_allocs,
            .peak_bytes = r->peak_bytes,
            .total_bytes = r->total_bytes,
            .alloc_count = r->alloc_count,
            .free_count = r->free_count,
        };
        cb(&bucket, user_data);
    }
    mel__track_unlock(t);
}
