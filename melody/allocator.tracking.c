#include "allocator.tracking.h"
#include "allocator.h"
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

        t->total_allocated += size;
        t->current_usage += size;
        t->alloc_count++;
        if (t->current_usage > t->peak_usage)
            t->peak_usage = t->current_usage;

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

        t->total_allocated += size;
        t->total_freed += old_size;
        t->current_usage = t->current_usage - old_size + size;
        t->alloc_count++;
        t->free_count++;
        if (t->current_usage > t->peak_usage)
            t->peak_usage = t->current_usage;

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

        t->total_freed += freed_size;
        t->current_usage -= freed_size;
        t->free_count++;
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
