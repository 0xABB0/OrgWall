#include "chord.h"
#include "interval_seq.h"
#include <stdlib.h>

void mel_chord_free(Mel_Chord* c)
{
  if (!c) return;
  mel_scale_free(&c->pitches);
}

Mel_Chord mel_chord_from_root(Mel_Pitch root, Mel_IntervalSeq pattern)
{
  Mel_Chord c;
  c.pitches = mel_interval_seq_to_scale(&pattern, root);
  return c;
}

Mel_Chord mel_chord_from_root_and_diffs(Mel_Pitch root, const int64_t* diffs, int32_t count)
{
  Mel_IntervalSeq pattern = mel_interval_seq_from_diffs(root.tuning, diffs, count);
  Mel_Chord c = mel_chord_from_root(root, pattern);
  mel_interval_seq_free(&pattern);
  return c;
}

Mel_IntervalSeq mel_chord_pattern_major_triad(const Mel_Tuning* tuning)
{
  uint64_t num[] = {5, 6};
  uint64_t den[] = {4, 5};
  return mel_interval_seq_from_ratios(tuning, num, den, 2);
}

Mel_IntervalSeq mel_chord_pattern_minor_triad(const Mel_Tuning* tuning)
{
  uint64_t num[] = {6, 5};
  uint64_t den[] = {5, 4};
  return mel_interval_seq_from_ratios(tuning, num, den, 2);
}

Mel_IntervalSeq mel_chord_pattern_diminished_triad(const Mel_Tuning* tuning)
{
  uint64_t num[] = {6, 6};
  uint64_t den[] = {5, 5};
  return mel_interval_seq_from_ratios(tuning, num, den, 2);
}

Mel_IntervalSeq mel_chord_pattern_augmented_triad(const Mel_Tuning* tuning)
{
  uint64_t num[] = {5, 5};
  uint64_t den[] = {4, 4};
  return mel_interval_seq_from_ratios(tuning, num, den, 2);
}

Mel_IntervalSeq mel_chord_pattern_major_seventh(const Mel_Tuning* tuning)
{
  uint64_t num[] = {5, 6, 15};
  uint64_t den[] = {4, 5, 8};
  return mel_interval_seq_from_ratios(tuning, num, den, 3);
}

Mel_IntervalSeq mel_chord_pattern_minor_seventh(const Mel_Tuning* tuning)
{
  uint64_t num[] = {6, 5, 4};
  uint64_t den[] = {5, 4, 3};
  return mel_interval_seq_from_ratios(tuning, num, den, 3);
}

Mel_IntervalSeq mel_chord_pattern_dominant_seventh(const Mel_Tuning* tuning)
{
  uint64_t num[] = {5, 6, 9};
  uint64_t den[] = {4, 5, 5};
  return mel_interval_seq_from_ratios(tuning, num, den, 3);
}

Mel_IntervalSeq mel_chord_pattern_diminished_seventh(const Mel_Tuning* tuning)
{
  uint64_t num[] = {6, 6, 6};
  uint64_t den[] = {5, 5, 5};
  return mel_interval_seq_from_ratios(tuning, num, den, 3);
}

int32_t mel_chord_size(const Mel_Chord* c)
{
  return c->pitches.count;
}

Mel_Pitch mel_chord_root(const Mel_Chord* c)
{
  return mel_scale_get(&c->pitches, 0);
}
