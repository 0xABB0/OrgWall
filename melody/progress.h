#pragma once

#include "allocator.fwd.h"
#include "collection.array.fwd.h"
#include "async.signal.fwd.h"
#include "core.types.h"

typedef struct Mel_Progress Mel_Progress;

typedef struct {
    f32  value;
    bool failed;
} Mel_Progress_Status;

typedef Mel_Progress_Status (*Mel_Progress_Fn)(void* user);

#define MEL_PROGRESS_SOURCE_CUSTOM  0
#define MEL_PROGRESS_SOURCE_COUNTER 1
#define MEL_PROGRESS_SOURCE_CHILD   2

typedef struct {
    u32   kind;
    f32   weight;
    union {
        struct {
            Mel_Progress_Fn fn;
            void*           user;
        } custom;
        struct {
            Mel_Counter* counter;
            i64          total;
        } counter;
        const Mel_Progress* child;
    };
} Mel_Progress_Source;

struct Mel_Progress {
    Mel_Array(Mel_Progress_Source) sources;
};

void                mel_progress_init(Mel_Progress* progress, const Mel_Alloc* alloc);
void                mel_progress_destroy(Mel_Progress* progress);
void                mel_progress_clear(Mel_Progress* progress);

void                mel_progress_add_custom(Mel_Progress* progress, Mel_Progress_Fn fn, void* user, f32 weight);
void                mel_progress_add_counter(Mel_Progress* progress, Mel_Counter* counter, i64 total, f32 weight);
void                mel_progress_add_child(Mel_Progress* progress, const Mel_Progress* child, f32 weight);

Mel_Progress_Status mel_progress_state(const Mel_Progress* progress);
f32                 mel_progress_value(const Mel_Progress* progress);
bool                mel_progress_is_ready(const Mel_Progress* progress, f32 threshold);
bool                mel_progress_is_failed(const Mel_Progress* progress);
