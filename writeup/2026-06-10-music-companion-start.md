# Music companion — milestone 5 start

## Work done

- New `pitchdetect` module: YIN fundamental-frequency estimation (CMNDF, absolute
  threshold, parabolic interpolation). `Mel_PitchDetector` sizes scratch once from the
  caller's allocator; `mel_pitch_detect` allocates nothing. Lag search bounded by the
  configured `[min_hz, max_hz]`. Six tests: pure sines within 1 cent at 48 kHz/2048,
  sawtooth (octave trap) within 2 cents, low amplitude, silence and white noise
  correctly unvoiced, successive windows independent.
- New `apps/music-companion`: boot-hosted GUI app (canonical setup.c shape), tabview
  with Chords / Tuner / Sightreading. The Chords tab is live end to end: connects to
  the first MIDI input, tracks held notes from note_on/note_off (velocity-0 note_on
  honored), spells held notes via the western notation preset, names dyads as
  intervals (`mel_nat_acc_interval_symbol`), and identifies 3+ note sets through
  `mel_chord_identify` with slash notation for inversions and an alternatives count.
  Tuner and Sightreading tabs are honest stubs stating what they wait on.
- Smoke-verified: app launches, stays alive, exits clean on kill.

## Kludges

- `Companion_State` is a global singleton mirroring midi-monitor's pattern; fine for a
  single-screen app, would need a rethink for multi-window.
- Chord/held labels use fixed `char[256]` snprintf buffers for display text. Display
  truncation is handled (`+N` overflow marker) but it is still bounded formatting.
- `held[128]` fixed array — sanctioned: MIDI note numbers are a protocol constant.
- Connect always opens device index 0; no device picker yet (midi-monitor has the
  enumerate-and-list pattern to copy when wanted).
- The app frees nothing at teardown (tuning/notation/catalog live for process
  lifetime); boot's exit path reclaims, but an `mel_app_on_exit` would be cleaner.

## CLAUDE.md suggestions

- None new; the new-app skill matched reality exactly.

## Suggestions

- Next bricks per design/music-companion.md: microphone capture (placement decision:
  inside `audio` vs sibling module — gates the tuner), then the `staff` renderer
  (gates sightreading).
- pitchdetect could later grow an FFT-accelerated difference function if profiling
  shows the naive loop matters on phones (MEL-ENGINE-VI); current cost is ~1M
  mul-adds per window at tuner settings.
