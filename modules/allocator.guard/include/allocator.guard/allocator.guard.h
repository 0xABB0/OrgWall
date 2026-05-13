#pragma once

#include "cfg.h"
#include "allocator.fwd.h"
#include "core.types.h"

typedef struct Mel_Guard_Allocator Mel_Guard_Allocator;

typedef struct {
    const Mel_Alloc* backing;
    usize pre_guard_size;
    usize post_guard_size;
    usize quarantine_bytes;
    usize page_protect_min_size;
    usize protected_overhead_budget;
    u32 sample_every;
    u32 flags;
} Mel_Guard_Allocator_Opt;

typedef struct {
    usize live_bytes;
    usize live_allocs;
    usize quarantined_bytes;
    usize quarantined_allocs;
    usize protected_bytes;
    usize protected_allocs;
    u64 alloc_index;
} Mel_Guard_Allocator_Stats;

struct Mel_Guard_Allocator {
    const Mel_Alloc* backing;

    usize pre_guard_size;
    usize post_guard_size;
    usize quarantine_bytes;
    usize page_protect_min_size;
    usize protected_overhead_budget;

    u32 sample_every;
    u32 flags;

    usize live_bytes;
    usize live_allocs;
    usize quarantined_bytes;
    usize quarantined_allocs;
    usize protected_bytes;
    usize protected_allocs;
    u64   alloc_index;

    struct Mel_Guard_Header* quarantine_head;
    struct Mel_Guard_Header* quarantine_tail;

    u32 lock;
    bool initialized;
};

void      mel_guard_init(Mel_Guard_Allocator* g, Mel_Guard_Allocator_Opt opt);
void      mel_guard_shutdown(Mel_Guard_Allocator* g);
Mel_Alloc mel_guard_allocator(Mel_Guard_Allocator* g);

void      mel_guard_check(Mel_Guard_Allocator* g);
Mel_Guard_Allocator_Stats mel_guard_stats(Mel_Guard_Allocator* g);
