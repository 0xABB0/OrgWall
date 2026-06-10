# Musictheory renovation

## Work done

Split the old musictheory monolith into four layers and made the per-note types
alloc-free:

- `frequency` absorbed `Mel_Cent` (cent.h/.inl/.c moved; it is a log-frequency unit and
  belongs next to `Mel_Hz`).
- New `tuning` module: `Mel_Tuning` is now behavior-parametrized
  (`{ctx, frequency_for_index, period, destroy, alloc}`) instead of a kind enum +
  switches (MEL-CODE-001). ed/edo precompute the step ratio once at construction
  (previously an mpfr nth-root on every single frequency query). Scala import/export
  moved here, rewritten on `str8` + caller allocators.
- `musictheory` slimmed to abstract pitch math. `Mel_Pitch` = `{tuning, index}` and
  `Mel_Interval` = `{tuning, ref_index, diff}` are trivially-copyable values; the old
  heap `mpfr_t` ratio cache is gone (it was derivable). Ratios/cents/frequencies
  compute on demand into `Mel_Real` scratch. `Mel_Scale` is a sorted unique
  `Mel_Array(i64)`; interval-seq became `Mel_Pattern` (step diffs). Iteration and set
  algebra are `coro` generators (`scale_gen.coro.h`): pitches, infinite stream,
  union/intersection/difference merges, complement, pattern expansion; eager set ops
  loop the same generators.
- New `notation` module: `Mel_Note` is a pure value `{pitch, nat_class, nat_bi_index,
  acc_value}` — symbols are generated on demand as `str8`, never stored. Symbol codes
  support zero-valued identity symbols (P/M); interval naming is fully implemented
  (the old `mel_nat_acc_interval` was a stub) with per-class reference steps so
  qualities are correct (E→C5 is m6, not M6). Enharmonic strategies got a `destroy`
  hook. `western.h`: 12-TET preset (index 0 = C4 = MIDI 60), named scale patterns,
  chord-quality catalog, and chord identification (`mel_chord_identify_g` +
  ranked eager wrapper).
- `midi` adapted; `mel_midi_pitch_to_note` no longer collapses octaves.
- Tests: 29 new tests across tuning-test / musictheory-test / notation-test, all green.

Bugs found in the old module (all fixed by the rewrite): infinite loop in
`mel_symbol_code_generate` (+4096-byte unchecked buffer); double free of interval
ratios in note-interval-seq; enharmonic ctx leak + blind `free`; dead branch dropping
pitches in `period_normalized`; negative-index OOB in `nat_acc_note_by_index`;
wrong spellings at period boundaries; stubbed interval functions; silent defaults on
parse failures and tuning mismatches; uninitialized-mpq crash on scala parse failure;
empty-chord OOB; scala parser treating the description line as the count;
custom-tuning step table off by one vs scala convention; `from_ratios` treating
successive step ratios as root-relative (major triad came out as {0,3,4} instead of
{0,4,7}); midi pitch→note dropping the octave via `pc_index`; truncating division
giving wrong pc/bi for negative indices.

## Kludges

- `mel_scale_contains_pc` is a linear scan; `pcs_complement` is O(period × count).
  Fine at musical sizes, would want a bitset for large periods.
- `mel_scale_rotation` materializes one intermediate scale per rotation step instead of
  rotating by modular arithmetic in one pass.
- `mel_symbol_code_generate` greedily renders multi-diminished intervals as "dm3"
  instead of "ddm…"-style conventional stacking. The old code had the same flaw.
- `mel_chord_identify` ranking is a fixed heuristic (root-position first, catalog
  order second); no scoring model.
- `Mel_Pattern_from_cents` quantizes through `mel_tuning_find_index` per element —
  correct but does a binary search per value.
- Test suites use file-scope singleton tunings that are never freed (process-lifetime,
  intentional in tests, still worth confessing).
- `mel_tuning_get_generators` kept the caller-buffer + max_count shape instead of a
  generator; it predates the coro work and I left its shape alone.

## CLAUDE.md suggestions

- Document the coro codegen wiring pattern (mel_codegen + private include of the
  header's own directory) — it cost one build iteration to discover.
- Consider noting that `Mel_Real`/`str8`/`Mel_Array` are the canonical value/string/
  container idioms for new modules, so future modules don't reinvent malloc surfaces.

## Suggestions

- `audio` has no capture path; the companion app needs it (see
  design/music-companion.md). Worth deciding whether capture lives inside `audio` or
  in a sibling module.
- `collection` readme is a single cryptic line ("mpmc = Massively Parallel Monte
  Carlo") — misleading, worth a real readme.
- midi-monitor could grow a chord-name readout now that `mel_chord_identify` exists —
  cheap demo of the new stack.
- The old `Mel_NoteScale`/`Mel_NoteIntervalSeq` wrappers were dropped; nothing consumed
  them. If spelled scales are needed later, build them as `Mel_Note` arrays + the
  enharmonic strategy rather than parallel container types.
