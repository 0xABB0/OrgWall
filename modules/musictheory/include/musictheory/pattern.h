#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <musictuning/tuning.h>

#include "pitch.h"
#include "scale.h"

typedef struct Mel_Pattern Mel_Pattern;

struct Mel_Pattern
{
    const Mel_Tuning* tuning;
    Mel_Index_Array   diffs;
};

void               mel_pattern_free(Mel_Pattern* p);
static inline void mel_pattern_cleanup(Mel_Pattern* p) { mel_pattern_free(p); }
#define Mel_Pattern_AUTO MEL_CLEANUP(mel_pattern_cleanup) Mel_Pattern

MEL_NODISCARD Mel_Pattern mel_pattern_make(const Mel_Alloc* alloc, const Mel_Tuning* tuning);

MEL_NODISCARD Mel_Pattern mel_pattern_from_diffs(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const i64* diffs, i32 count);

MEL_NODISCARD Mel_Pattern mel_pattern_from_ratios(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const u64* nums, const u64* dens, i32 count);

MEL_NODISCARD Mel_Pattern mel_pattern_from_cents(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const f64* cents, i32 count);

MEL_NODISCARD Mel_Pattern mel_pattern_from_scale(const Mel_Alloc* alloc, const Mel_Scale* s);

MEL_NODISCARD Mel_Pattern mel_pattern_copy(const Mel_Alloc* alloc, const Mel_Pattern* p);

void mel_pattern_add(Mel_Pattern* p, i64 diff);

MEL_NODISCARD static inline i32 mel_pattern_count(const Mel_Pattern* p) { return (i32)p->diffs.count; }

MEL_NODISCARD i64 mel_pattern_get(const Mel_Pattern* p, i32 idx);

MEL_NODISCARD i64 mel_pattern_span(const Mel_Pattern* p);

MEL_NODISCARD Mel_Pattern mel_pattern_concat(const Mel_Alloc* alloc, const Mel_Pattern* a, const Mel_Pattern* b);

MEL_NODISCARD Mel_Pattern mel_pattern_repeat(const Mel_Alloc* alloc, const Mel_Pattern* p, i32 times);

MEL_NODISCARD Mel_Pattern mel_pattern_retrograde(const Mel_Alloc* alloc, const Mel_Pattern* p);

MEL_NODISCARD Mel_Scale mel_pattern_to_scale(const Mel_Alloc* alloc, const Mel_Pattern* p, Mel_Pitch root);
