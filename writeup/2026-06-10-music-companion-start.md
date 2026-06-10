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
- New `audiocapture` module (sibling of `audio`, per Gabbo's call): microphone input as
  a pull stream. CoreAudio HAL device enumeration (ids + per-id `str8` names),
  default-device query, AudioQueue input writing f32 mono frames into a private
  lock-free SPSC ring, permission state as two boolean queries (no status enum).
  macOS only; the rest of the platforms, async authorize, and hotplug are recorded in
  todo.md. Three tests (ring semantics incl. wrap/bounds, live enumeration smoke).
- Tuner tab wired end to end: Start opens the default input (48 kHz mono, system mic
  prompt fires on first use), a vat tick slides a 2048-sample window with 1024 hop
  through YIN, and the display shows nearest note name, signed cents deviation,
  detected Hz, and clarity; unvoiced input decays to "listening..." after a hold.

## Kludges

- `Companion_State` is a global singleton mirroring midi-monitor's pattern; fine for a
  single-screen app, would need a rethink for multi-window.
- Chord/held labels use fixed `char[256]` snprintf buffers for display text. Display
  truncation is handled (`+N` overflow marker) but it is still bounded formatting.
- `held[128]` fixed array — sanctioned: MIDI note numbers are a protocol constant.
- Connect always opens device index 0; no device picker yet (midi-monitor has the
  enumerate-and-list pattern to copy when wanted).
- The app frees nothing at teardown (tuning/notation/catalog/detector live for process
  lifetime); boot's exit path reclaims, but an `mel_app_on_exit` would be cleaner.
- audiocapture's SPSC ring duplicates the shape of audio's private `Mel_Audio_Ring`
  (sanctioned by the sibling-module decision); extraction to a shared home is in
  audiocapture/todo.md.
- The ring copy loop is per-sample modulo, not two memcpy spans; capture rates make it
  irrelevant, but it is lazier than it should be.
- Tuner UI updates on every voiced window (~21 ms) with no smoothing/median filter;
  a real tuner wants a small estimate history.
- The tuner-tab live path (real mic, real signal, TCC prompt) has NOT been exercised —
  only app liveness and device enumeration were verified unattended. First manual run
  is owed; the DSP underneath is covered by pitchdetect's synthesized tests.

## CLAUDE.md suggestions

- None new; the new-app skill matched reality exactly.

## Suggestions

- Next bricks per design/music-companion.md: microphone capture (placement decision:
  inside `audio` vs sibling module — gates the tuner), then the `staff` renderer
  (gates sightreading).
- pitchdetect could later grow an FFT-accelerated difference function if profiling
  shows the naive loop matters on phones (MEL-ENGINE-VI); current cost is ~1M
  mul-adds per window at tuner settings.
