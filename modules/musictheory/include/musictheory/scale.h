#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <collection/array.h>
#include <tuning/tuning.h>

#include "pitch.h"
#include "interval.h"

typedef Mel_Array(i64) Mel_Index_Array;

typedef struct Mel_Scale Mel_Scale;

struct Mel_Scale
{
    const Mel_Tuning* tuning;
    Mel_Index_Array   indices;
};

void               mel_scale_free(Mel_Scale* s);
static inline void mel_scale_cleanup(Mel_Scale* s) { mel_scale_free(s); }
#define Mel_Scale_AUTO MEL_CLEANUP(mel_scale_cleanup) Mel_Scale

MEL_NODISCARD Mel_Scale mel_scale_make(const Mel_Alloc* alloc, const Mel_Tuning* tuning);

MEL_NODISCARD Mel_Scale mel_scale_from_indices(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const i64* indices, i32 count);

MEL_NODISCARD Mel_Scale mel_scale_from_pitches(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const Mel_Pitch* pitches, i32 count);

MEL_NODISCARD Mel_Scale mel_scale_copy(const Mel_Alloc* alloc, const Mel_Scale* s);

MEL_NODISCARD static inline i32 mel_scale_count(const Mel_Scale* s) { return (i32)s->indices.count; }

MEL_NODISCARD i64       mel_scale_index_at(const Mel_Scale* s, i32 idx);
MEL_NODISCARD Mel_Pitch mel_scale_at(const Mel_Scale* s, i32 idx);

void mel_scale_add_index(Mel_Scale* s, i64 index);
void mel_scale_add_pitch(Mel_Scale* s, Mel_Pitch pitch);

MEL_NODISCARD u8 mel_scale_contains_index(const Mel_Scale* s, i64 index);
MEL_NODISCARD u8 mel_scale_contains_pitch(const Mel_Scale* s, Mel_Pitch pitch);
MEL_NODISCARD u8 mel_scale_contains_pc(const Mel_Scale* s, i64 pc);

MEL_NODISCARD Mel_Interval mel_scale_spec_interval(const Mel_Scale* s, i32 source_idx, i32 target_idx);

void mel_scale_indices(const Mel_Scale* s, i64* out_indices);

MEL_NODISCARD Mel_Scale mel_scale_transpose(const Mel_Alloc* alloc, const Mel_Scale* s, i64 diff);

MEL_NODISCARD Mel_Scale mel_scale_union(const Mel_Alloc* alloc, const Mel_Scale* a, const Mel_Scale* b);
MEL_NODISCARD Mel_Scale mel_scale_intersection(const Mel_Alloc* alloc, const Mel_Scale* a, const Mel_Scale* b);
MEL_NODISCARD Mel_Scale mel_scale_difference(const Mel_Alloc* alloc, const Mel_Scale* a, const Mel_Scale* b);

MEL_NODISCARD u8 mel_scale_is_subset(const Mel_Scale* a, const Mel_Scale* b);
MEL_NODISCARD u8 mel_scale_eq(const Mel_Scale* a, const Mel_Scale* b);
MEL_NODISCARD u8 mel_scale_is_set_equivalent(const Mel_Scale* a, const Mel_Scale* b);

MEL_NODISCARD Mel_Scale mel_scale_reflect(const Mel_Alloc* alloc, const Mel_Scale* s, Mel_Pitch axis);

MEL_NODISCARD Mel_Scale mel_scale_zero_normalized(const Mel_Alloc* alloc, const Mel_Scale* s);
MEL_NODISCARD Mel_Scale mel_scale_pcs_normalized(const Mel_Alloc* alloc, const Mel_Scale* s);
MEL_NODISCARD Mel_Scale mel_scale_period_normalized(const Mel_Alloc* alloc, const Mel_Scale* s);
MEL_NODISCARD Mel_Scale mel_scale_pcs_complement(const Mel_Alloc* alloc, const Mel_Scale* s);

MEL_NODISCARD Mel_Scale mel_scale_rotated_up(const Mel_Alloc* alloc, const Mel_Scale* s);
MEL_NODISCARD Mel_Scale mel_scale_rotated_down(const Mel_Alloc* alloc, const Mel_Scale* s);
MEL_NODISCARD Mel_Scale mel_scale_rotation(const Mel_Alloc* alloc, const Mel_Scale* s, i32 order);
