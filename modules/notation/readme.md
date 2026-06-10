# notation

The spelling layer over `musictheory`: note names, accidentals, interval names, and
chord identification. A `Mel_Note` is a pure value — `{pitch, nat_class, nat_bi_index,
acc_value}` — symbols are generated on demand into a caller allocator as `str8`.

- `Mel_SymbolCode`: additive symbol systems (accidentals, interval qualities).
  Zero-valued entries act as identity symbols ("P", "M"). Parsing and generation are
  total: both report failure instead of guessing.
- `Mel_NatAccNotation`: naturals + accidentals notation (letter names). Parsing returns
  `bool`; interval computation produces natural diff, conventional number, and quality.
- `Mel_EnharmonicStrategy`: pluggable spelling policy with a `destroy` hook; the
  pc-blueprint strategy spells each pitch class from a template.
- `western.h`: 12-TET preset — `mel_tuning_western(alloc, a4)` (index 0 = C4, MIDI 60),
  `mel_notation_western`, named scale patterns (major, minors, modes, pentatonics,
  blues, chromatic), and a chord-quality catalog (triads through ninths).
- `chord_id.h` + `identify.coro.h`: chord identification over an open quality catalog.
  `mel_chord_identify_g` yields `{root_pc, quality, bass_member}` candidates for a
  pcs-normalized pitch-class scale; the eager wrapper collects and ranks them
  (root-position first, catalog order second).

Dependencies: core, math, frequency, tuning, musictheory, allocator, collection,
string, coro.
