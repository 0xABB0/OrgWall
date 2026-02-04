#pragma once


#include "allocator.h"

struct Mel_Tracking_Allocator
{
    const Mel_Alloc* backing;
    usize total_allocated;
    usize total_freed;
    usize current_usage;
    usize peak_usage;
    u64 alloc_count;
    u64 free_count;
};
typedef struct Mel_Tracking_Allocator Mel_Tracking_Allocator;

void mel_tracking_init(Mel_Tracking_Allocator* t, const Mel_Alloc* backing);
Mel_Alloc mel_tracking_allocator(Mel_Tracking_Allocator* t);
void mel_tracking_report(Mel_Tracking_Allocator* t);
