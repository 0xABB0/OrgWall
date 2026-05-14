#include <allocator.tracking/allocator.tracking.h>
#include <allocator/allocator.h>
#include <stdio.h>
#include <tracy/TracyC.h>

typedef struct {
    usize size;
} Mel_Tracking_Header;

static void* tracking_user_to_header(void* user_ptr)
{
    return ((Mel_Tracking_Header*)user_ptr) - 1;
}

static void* tracking_header_to_user(void* header_ptr)
{
    return ((Mel_Tracking_Header*)header_ptr) + 1;
}

static void mel__tracking_update_peak(Mel_Tracking_Allocator* t, usize current)
{
    usize peak = atomic_load_explicit(&t->peak_usage, memory_order_relaxed);
    while (current > peak) {
        if (atomic_compare_exchange_weak_explicit(&t->peak_usage, &peak, current,
                memory_order_relaxed, memory_order_relaxed))
            break;
    }
}

static void* tracking_alloc_cb(void* ptr, usize size, u32 align,
                               const char* file, const char* func, u32 line,
                               void* user_data)
{
    Mel_Tracking_Allocator* t = (Mel_Tracking_Allocator*)user_data;

    if (ptr == NULL && size > 0)
    {
        TracyCZoneN(ctx, "tracking_alloc", true);
        usize total = sizeof(Mel_Tracking_Header) + size;
        void* raw = t->backing->alloc_cb(NULL, total, align, file, func, line, t->backing->user_data);
        if (!raw) { TracyCZoneEnd(ctx); return NULL; }

        ((Mel_Tracking_Header*)raw)->size = size;

        atomic_fetch_add_explicit(&t->total_allocated, size, memory_order_relaxed);
        usize current = atomic_fetch_add_explicit(&t->current_usage, size, memory_order_relaxed) + size;
        atomic_fetch_add_explicit(&t->alloc_count, 1, memory_order_relaxed);
        mel__tracking_update_peak(t, current);

        TracyCAllocN(tracking_header_to_user(raw), size, "tracked");
        TracyCZoneEnd(ctx);
        return tracking_header_to_user(raw);
    }

    if (ptr != NULL && size > 0)
    {
        TracyCZoneN(ctx, "tracking_realloc", true);
        Mel_Tracking_Header* old_header = tracking_user_to_header(ptr);
        usize old_size = old_header->size;
        usize total = sizeof(Mel_Tracking_Header) + size;

        TracyCFreeN(ptr, "tracked");
        void* raw = t->backing->alloc_cb(old_header, total, align, file, func, line, t->backing->user_data);
        if (!raw) { TracyCZoneEnd(ctx); return NULL; }

        ((Mel_Tracking_Header*)raw)->size = size;

        atomic_fetch_add_explicit(&t->total_allocated, size, memory_order_relaxed);
        atomic_fetch_add_explicit(&t->total_freed, old_size, memory_order_relaxed);
        usize current;
        if (size >= old_size)
            current = atomic_fetch_add_explicit(&t->current_usage, size - old_size, memory_order_relaxed) + (size - old_size);
        else
            current = atomic_fetch_sub_explicit(&t->current_usage, old_size - size, memory_order_relaxed) - (old_size - size);
        atomic_fetch_add_explicit(&t->alloc_count, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&t->free_count, 1, memory_order_relaxed);
        mel__tracking_update_peak(t, current);

        TracyCAllocN(tracking_header_to_user(raw), size, "tracked");
        TracyCZoneEnd(ctx);
        return tracking_header_to_user(raw);
    }

    if (ptr != NULL && size == 0)
    {
        TracyCZoneN(ctx, "tracking_free", true);
        Mel_Tracking_Header* header = tracking_user_to_header(ptr);
        usize freed_size = header->size;

        TracyCFreeN(ptr, "tracked");
        t->backing->alloc_cb(header, 0, align, file, func, line, t->backing->user_data);

        atomic_fetch_add_explicit(&t->total_freed, freed_size, memory_order_relaxed);
        atomic_fetch_sub_explicit(&t->current_usage, freed_size, memory_order_relaxed);
        atomic_fetch_add_explicit(&t->free_count, 1, memory_order_relaxed);
        TracyCZoneEnd(ctx);
        return NULL;
    }

    return NULL;
}

void mel_tracking_init(Mel_Tracking_Allocator* t, const Mel_Alloc* backing)
{
    assert(t != NULL);
    assert(backing != NULL);
    t->backing = backing;
    atomic_store_explicit(&t->total_allocated, 0, memory_order_relaxed);
    atomic_store_explicit(&t->total_freed, 0, memory_order_relaxed);
    atomic_store_explicit(&t->current_usage, 0, memory_order_relaxed);
    atomic_store_explicit(&t->peak_usage, 0, memory_order_relaxed);
    atomic_store_explicit(&t->alloc_count, 0, memory_order_relaxed);
    atomic_store_explicit(&t->free_count, 0, memory_order_relaxed);
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
    usize total_alloc = atomic_load_explicit(&t->total_allocated, memory_order_relaxed);
    usize total_free = atomic_load_explicit(&t->total_freed, memory_order_relaxed);
    usize current = atomic_load_explicit(&t->current_usage, memory_order_relaxed);
    usize peak = atomic_load_explicit(&t->peak_usage, memory_order_relaxed);
    u64 allocs = atomic_load_explicit(&t->alloc_count, memory_order_relaxed);
    u64 frees = atomic_load_explicit(&t->free_count, memory_order_relaxed);

    printf("=== Memory Tracking Report ===\n");
    printf("Total allocated: %zu bytes\n", total_alloc);
    printf("Total freed:     %zu bytes\n", total_free);
    printf("Current usage:   %zu bytes\n", current);
    printf("Peak usage:      %zu bytes\n", peak);
    printf("Alloc count:     %llu\n", allocs);
    printf("Free count:      %llu\n", frees);
    if (current > 0)
    {
        printf("WARNING: %zu bytes still allocated (potential leak)\n", current);
    }
    printf("==============================\n");
}
