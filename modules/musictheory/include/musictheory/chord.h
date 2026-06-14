#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <musictuning/tuning.h>

#include "pitch.h"
#include "pattern.h"
#include "scale.h"

typedef struct Mel_Chord Mel_Chord;

struct Mel_Chord
{
    Mel_Scale pitches;
    i64       root_index;
};

#ifdef __cplusplus
extern "C"
{
#endif

void               mel_chord_free(Mel_Chord* c);
static inline void mel_chord_cleanup(Mel_Chord* c) { mel_chord_free(c); }
#define Mel_Chord_AUTO MEL_CLEANUP(mel_chord_cleanup) Mel_Chord

MEL_NODISCARD Mel_Chord mel_chord_from_root(const Mel_Alloc* alloc, Mel_Pitch root, const Mel_Pattern* pattern);

MEL_NODISCARD Mel_Chord mel_chord_from_root_and_diffs(const Mel_Alloc* alloc, Mel_Pitch root, const i64* diffs, i32 count);

MEL_NODISCARD Mel_Chord mel_chord_copy(const Mel_Alloc* alloc, const Mel_Chord* c);

MEL_NODISCARD static inline i32 mel_chord_size(const Mel_Chord* c) { return mel_scale_count(&c->pitches); }

MEL_NODISCARD Mel_Pitch mel_chord_root(const Mel_Chord* c);

MEL_NODISCARD Mel_Pitch mel_chord_at(const Mel_Chord* c, i32 idx);

MEL_NODISCARD Mel_Pattern mel_chord_pattern_major_triad(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_chord_pattern_minor_triad(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_chord_pattern_diminished_triad(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_chord_pattern_augmented_triad(const Mel_Alloc* alloc, const Mel_Tuning* tuning);

MEL_NODISCARD Mel_Pattern mel_chord_pattern_major_seventh(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_chord_pattern_minor_seventh(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_chord_pattern_dominant_seventh(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_chord_pattern_diminished_seventh(const Mel_Alloc* alloc, const Mel_Tuning* tuning);

#ifdef __cplusplus
}
#endif
