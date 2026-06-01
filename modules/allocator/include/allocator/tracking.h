#pragma once

#include "tracking.cfg.h"
#include <allocator/allocator.fwd.h>
#include <core/types.h>

typedef struct Mel_Track_Allocator Mel_Track_Allocator;

struct Mel_Track_Slot;

typedef struct
{
    struct Mel_Track_Slot* slots;
    usize                  count;
    usize                  capacity;
} Mel_Track_Map;

typedef struct
{
    const Mel_Alloc* backing;
    const Mel_Alloc* meta;
    u32              flags;
    u32              backtrace_depth;
} Mel_Track_Allocator_Opt;

typedef struct
{
    usize live_bytes;
    usize live_allocs;
    usize peak_bytes;
    usize peak_allocs;
    usize total_alloc_bytes;
    usize total_alloc_count;
    usize total_free_count;
    usize total_realloc_count;
} Mel_Track_Allocator_Stats;

typedef struct
{
    const char*  file;
    const char*  func;
    u32          line;
    usize        size;
    const char*  tag;
    u64          seq;
    void* const* frames;
    usize        frame_count;
} Mel_Track_Record;

typedef struct
{
    const char* key;
    u32         line;
    usize       live_bytes;
    usize       live_allocs;
    usize       peak_bytes;
    usize       total_bytes;
    usize       alloc_count;
    usize       free_count;
} Mel_Track_Bucket;

typedef void (*Mel_Track_Report_Cb)(const Mel_Track_Record* rec, void* user_data);
typedef void (*Mel_Track_Bucket_Cb)(const Mel_Track_Bucket* bucket, void* user_data);

struct Mel_Track_Allocator
{
    const Mel_Alloc* backing;
    const Mel_Alloc* meta;
    u32              flags;
    u32              backtrace_depth;

    usize live_bytes;
    usize live_allocs;
    usize peak_bytes;
    usize peak_allocs;
    usize total_alloc_bytes;
    usize total_alloc_count;
    usize total_free_count;
    usize total_realloc_count;
    u64   seq;

    Mel_Track_Map registry;
    Mel_Track_Map sites;
    Mel_Track_Map tags;

    u32  lock;
    bool initialized;
};

void      mel_track_init(Mel_Track_Allocator* t, Mel_Track_Allocator_Opt opt);
void      mel_track_shutdown(Mel_Track_Allocator* t);
Mel_Alloc mel_track_allocator(Mel_Track_Allocator* t);

Mel_Track_Allocator_Stats mel_track_stats(Mel_Track_Allocator* t);

void mel_track_dump_live(Mel_Track_Allocator* t, Mel_Track_Report_Cb cb, void* user_data);
void mel_track_dump_sites(Mel_Track_Allocator* t, Mel_Track_Bucket_Cb cb, void* user_data);
void mel_track_dump_tags(Mel_Track_Allocator* t, Mel_Track_Bucket_Cb cb, void* user_data);

void mel_track_scope_push(const char* tag);
void mel_track_scope_pop(void);
