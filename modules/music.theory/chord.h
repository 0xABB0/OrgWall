#pragma once

#include "tuning.h"
#include "interval.h"
#include "interval_seq.h"
#include "scale.h"

typedef struct Mel_Chord Mel_Chord;

struct Mel_Chord
{
  Mel_Scale pitches;
};

void mel_chord_free(Mel_Chord* c);
static inline void mel_chord_cleanup(Mel_Chord* c) { mel_chord_free(c); }
#define Mel_Chord_AUTO __attribute__((cleanup(mel_chord_cleanup))) Mel_Chord

Mel_Chord mel_chord_from_root(Mel_Pitch root, Mel_IntervalSeq pattern);

Mel_Chord mel_chord_from_root_and_diffs(Mel_Pitch root, const int64_t* diffs, int32_t count);

Mel_IntervalSeq mel_chord_pattern_major_triad(const Mel_Tuning* tuning);
Mel_IntervalSeq mel_chord_pattern_minor_triad(const Mel_Tuning* tuning);
Mel_IntervalSeq mel_chord_pattern_diminished_triad(const Mel_Tuning* tuning);
Mel_IntervalSeq mel_chord_pattern_augmented_triad(const Mel_Tuning* tuning);

Mel_IntervalSeq mel_chord_pattern_major_seventh(const Mel_Tuning* tuning);
Mel_IntervalSeq mel_chord_pattern_minor_seventh(const Mel_Tuning* tuning);
Mel_IntervalSeq mel_chord_pattern_dominant_seventh(const Mel_Tuning* tuning);
Mel_IntervalSeq mel_chord_pattern_diminished_seventh(const Mel_Tuning* tuning);

int32_t mel_chord_size(const Mel_Chord* c);
Mel_Pitch mel_chord_root(const Mel_Chord* c);
