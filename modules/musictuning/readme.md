# tuning

Tuning systems: the mapping between abstract pitch indices and physical frequencies.

`Mel_Tuning` is behavior-parametrized — `{ctx, frequency_for_index, period, destroy}` —
so any temperament is constructible without touching this module. Provided constructors:

- `mel_tuning_ed(alloc, divisions, eq_num, eq_den, ref)` — equal division of an
  arbitrary ratio; the step ratio is computed once at construction.
- `mel_tuning_edo(alloc, divisions, ref)` — equal division of the octave.
- `mel_tuning_custom(alloc, steps_count, ref)` — explicit step table. `steps[0]` is
  the unison (1/1); `steps[k]` is the ratio of degree `k`; the equivalence ratio
  defaults to 2/1 until `mel_tuning_custom_set_eq_ratio` is called.

`period == 0` means aperiodic. `frequency_for_index` must be monotonic in the index;
`mel_tuning_find_index` relies on it.

`scala.h` parses and exports the Scala `.scl` format (`str8` in, `str8` out, caller
allocator). Export works for any periodic tuning by sampling one period.

Dependencies: core, math, frequency, allocator, collection, string.
