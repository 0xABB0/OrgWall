#include "allocator.tracking.h"
#include "allocator.h"
#include <stdio.h>

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
