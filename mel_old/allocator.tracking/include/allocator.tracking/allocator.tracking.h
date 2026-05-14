#pragma once

#include <core/types.h>
#include <allocator/fwd.h>
#include <stdatomic.h>

typedef struct Mel_Tracking_Allocator {
    const Mel_Alloc* backing;
    _Atomic(usize) total_allocated;
    _Atomic(usize) total_freed;
    _Atomic(usize) current_usage;
    _Atomic(usize) peak_usage;
    _Atomic(u64)   alloc_count;
    _Atomic(u64)   free_count;
} Mel_Tracking_Allocator;

void      mel_tracking_init(Mel_Tracking_Allocator* t, const Mel_Alloc* backing);
Mel_Alloc mel_tracking_allocator(Mel_Tracking_Allocator* t);
void      mel_tracking_report(Mel_Tracking_Allocator* t);
