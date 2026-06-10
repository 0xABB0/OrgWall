# Music Companion

A musician's everyday toolbox app, built on `tuning`/`musictheory`/`notation`. It is
the proving ground for those modules: every feature consumes them through the public
API only.

## Features

### Tuner
- Microphone capture → fundamental pitch estimate → nearest pitch in the active tuning,
  deviation shown in cents (`Mel_Cent`, `mel_tuning_find_index`).
- Selectable reference (A4 frequency) and tuning system (any `Mel_Tuning`, scala import).
- Needle/strobe display; note name via `mel_notation_guess_note`.
- Instrument presets (guitar/violin/etc.) as pitch sets to tune against.

### Sightreading exercise generator
- Generates note sequences from a chosen scale/range/difficulty using
  `mel_scale_stream_g` and `mel_pattern_pitches_g` (alloc-free infinite feeds).
- Renders staff notation (clef, key signature, accidentals from `Mel_Note` spelling).
- Two checking modes: MIDI input (exact) and microphone (tuner pipeline reused).
- Difficulty axes: range, accidental density, interval leaps, rhythm (later).
- Progress tracking per session; mistakes drive the next exercise's bias.

### Chord analyzer
- MIDI device attached: held notes → pitch-class set → `mel_chord_identify_g`
  candidates ranked (root position first), spelled via the notation layer
  ("Cmaj7", "F#min7b5/A").
- Live display of held notes on a staff plus candidate list with inversions.
- History tape of recognized chords; tap to replay through audio synth (later).

### Shared
- Tuning-system picker (EDO/ED/custom/scala files) applied app-wide.
- Reference-pitch setting (A4) applied app-wide.

## Modules required

### Existing, used as-is
- `tuning`, `musictheory`, `notation` — theory stack.
- `midi` — input devices, note on/off, already converts to `Mel_Pitch`.
- `gui` (tabview, labels, canvas, sliders), `paint`, `color` — UI.
- `boot`, `vat`, `allocator`, `collection`, `string`, `log` — app plumbing.
- `audio` — output for replay/reference tones.
- `fs`/`io` — scala file loading, progress persistence.

### New modules needed
- `audiocapture` (or capture support inside `audio`): microphone input as a pull/ring
  stream with device enumeration and permission handling per platform (CoreAudio,
  WASAPI, ALSA, AAudio, Web Audio). The `audio` module is currently playback-only.
- `pitchdetect`: fundamental-frequency estimation over PCM windows (YIN or MPM/
  McLeod; autocorrelation baseline). Input: f32 frames; output: `Mel_Hz` + confidence.
  Alloc-free hot path (caller-provided scratch), no platform code.
- `staff`: staff-notation layout and rendering primitives over `paint`/`gui` canvas —
  clefs, key signatures, note heads/stems/beams, accidentals, ledger lines. Consumes
  `Mel_Note` spellings; knows nothing about exercises.
- `exercise` (candidate, may start inside the app): exercise generation policy,
  difficulty model, session scoring. Pure logic over `musictheory` generators; promote
  to a module once a second consumer exists.

### App target
- `apps/music-companion`: tabbed GUI (Tuner | Sightreading | Chords), settings pane,
  wiring between capture/midi streams and the theory stack.

## Build order (no-prerequisite-first)
1. `pitchdetect` (pure DSP, testable offline with synthesized PCM).
2. `audiocapture` (platform work; macOS first, then the rest).
3. `staff` (pure rendering, testable with golden images).
4. `apps/music-companion` shell with chord analyzer (midi already works end to end).
5. Tuner (capture + pitchdetect ready).
6. Sightreading (staff + exercise policy).
