# musictheory

Abstract pitch mathematics over a tuning: pitches, intervals, scales, patterns, chords.
Spelling (note names, accidentals) lives in the `notation` module; tuning systems live
in `tuning`.

- `Mel_Pitch` and `Mel_Interval` are trivially-copyable values: `{tuning, index}` and
  `{tuning, ref_index, diff}`. Frequencies, ratios and cents are computed on demand —
  nothing is cached, nothing allocates.
- `Mel_Scale` is a sorted unique index set; `Mel_Pattern` is a step-diff sequence
  (scale shapes, chord qualities, arpeggios). Both store the caller's allocator.
- `Mel_Chord` is a scale plus a root index (the root need not be the lowest pitch).
- `scale_gen.coro.h` exposes iteration as alloc-free `coro` generators:
  `mel_scale_pitches_g`, infinite `mel_scale_stream_g` (pcs-normalized scale in,
  endless ascending pitches out), set-algebra merges (`union/intersection/difference`),
  `mel_scale_complement_g`, and `mel_pattern_pitches_g`. The eager `Mel_Scale`-returning
  set operations loop the same generators.

Contracts assert in debug: tuning mismatches, out-of-range indices, empty-chord root,
period-requiring operations on aperiodic tunings.

Dependencies: core, math, frequency, tuning, allocator, collection, coro.
