# 2026-06-11 — audio stack wireframes (pcm, spectrum, audioin, audioout, audiopolicy, audiocapture, audioplayback, audio, tts, stt)

## Work done

Wireframe session (`/wireframe`), no implementation. The interview ran twice:
a first pass produced a microphone/speaker draft that Gabbo rejected for
under-asking (the camera pattern was cloned as if precedent were decision —
handle model, descriptor shapes, and surface scope were never his). The
second pass walked the tree properly; every decision below is
interview-confirmed. Final trios (spec.md + headers + usage.c), all on the
`worktree-audio-v2-wireframes` branch, uncommitted; main untouched.

Module set (10):

- **`pcm`** (new) — wait-free SPSC *frame* ring (frame-granular; fixes the
  channel-desync hazard of the two private sample rings), resampler
  contract + linear kernel, interleave/convert. core+allocator only.
- **`spectrum`** (new) — real FFT, window functions as open function set,
  bin↔Hz; the analysis gap beside pitchdetect. Visualizer = read → window →
  analyze.
- **`audioin`** (new) — input identity: enumeration/describe/default/
  hotplug, consent (sole owner of the prompt), gain, loopback as a device
  kind (system-audio capture), provider registry (host OS = provider 0,
  virtual devices first-class, push-plane sinks), publish (app-created
  inputs; OS-visible on PipeWire/macOS-HAL-component, honest
  `WARN_LOCAL_ONLY` elsewhere), handle + stable id + find() identity.
- **`audioout`** (new) — output identity twin: volume/mute instead of
  consent, pull-plane providers (provider owns the clock), publish
  (virtual-cable read side).
- **`audiopolicy`** (new) — session category/mode, port override, bluetooth
  options, mix/duck, audio focus, interruptions, route events; honest-absent
  on desktop, named-warned lowering everywhere.
- **`audiocapture`** (rework) — pure pull stream over `Mel_AudioIn`: exact
  requested format (one conversion in core via pcm, all providers), voice
  processing (AEC/NS/AGC) + exclusive mode with honest `granted` readback,
  per-buffer timestamps (`read_ex`), sticky LOST, measured OVERRUN.
- **`audioplayback`** (new) — the thin output stream, twin of audiocapture
  (`audiocapture : audioin :: audioplayback : audioout`): write-ring mode or
  direct pull-callback mode, exclusive, latency query, measured underruns.
  Custom engines (a DAW graph, a synth) reach devices without the mixer;
  the mixer itself becomes a pull-mode client of this door.
- **`audio`** (delta) — `Mel_AudioOut` binding (pin-or-follow; requires
  `mel_audioout_init`; device plane = audioplayback pull mode — single
  provider-routed path, no side door), live `set_device`,
  loss/interruption = event + hold (auto-resume only on the OS's
  should-resume signal), output + per-voice taps (`tap.h`), generic pull
  source.
- **`tts`** (new, replaces speech's half) — voices, speak, SSML, word
  ranges, visemes, pause/resume/abort, offline render-to-PCM; no consent
  machinery at all.
- **`stt`** (new, replaces speech's half) — recognizers, three audio doors
  (OS default / chosen `Mel_AudioIn` incl. virtual / fed PCM), vocabulary
  biasing, punctuation/profanity options, require-on-device as a hard
  guarantee, consent composed from audioin.
- **`speech`** — declared replaced by tts+stt; folder untouched until the
  implementation pass migrates backends.

Key interview decisions: identity/stream split with identity modules
(settings panel links zero stream code); virtual devices first-class via
provider registries in both identity modules; handle (session) + stable id
(persistence) identity; mic providers push / speaker providers pull on their
own clock; terminology `audioin`/`audioout`/`audiopolicy` over
microphone/speaker/audiosession (loopback and HDMI made the warm names lie);
tts/stt split (fed sessions + virtual mics dissolved the coupling);
audiocapture kept separate (seam symmetry); all four capability packs in
scope (visualizer, OS-publish, pro-capture, speech extras); audioplayback
added after the DAW stress test exposed the missing output door; the DAW
tiers (audiograph, transport, duplex sync, pluginhost, codec, diskstream,
musicsync, ASIO/JACK providers) named and bounded in `design/audio-daw.md`
for their own wireframe interviews.

## Kludges

- **`audiocapture.h` is a breaking rewrite** and `audio`'s headers changed
  (`Mel_Audio_Opt` grew a device field, `engine.h` now includes
  audioout/pcm): the audio module sources, audiocapture macos backend, its
  test, and `apps/music-companion` do not compile against the worktree
  headers until implementation. Branch-only; main is clean.
- **Stale prose**: `modules/audiocapture/readme.md` + `todo.md`,
  `modules/audio/readme.md`, and all of `modules/speech/` (readme, spec,
  todo, headers) now describe surfaces the trios supersede. Left for the
  implementation pass.
- `audio/ownership.h` still holds a true enum (pre-existing MEL-CODE-001
  tension), out of scope, flagged again.
- `pcm` and the identity modules use multiple public headers rather than the
  wireframe skill's single `<m>/<m>.h`; provider contracts deserve their own
  header (matches camera/speech precedent).
- The first-pass interview failure itself: one round of trios was written
  and discarded. Process debt, recorded so the lesson survives the session.

## CLAUDE.md suggestions (recommendations only)

- The wireframe skill should state explicitly that existing-module patterns
  (camera et al.) are *candidate recommendations to present*, never
  decisions to clone silently — the "answerable by exploring" rule keeps
  getting stretched to cover design choices.
- The skill could also state how reworks treat live headers (rewrite in
  place and accept branch staleness vs additive-only).

## Suggestions

- Implementation order: pcm → spectrum → audioin → audioout → audiopolicy →
  audiocapture → audio delta → tts → stt; each with its mock-provider test
  per spec; speech backends migrate into tts/stt; music-companion migrates
  at the audiocapture step; hello-speech becomes hello-tts/hello-stt (or one
  combined showcase).
- The macOS HAL plug-in component (publish OS-visibility) and any win32
  driver story deserve their own design doc before implementation.
- `design/audio-mixer-core.md` should fold into `modules/audio/` per
  MEL-SPEC-002.
- Owed, recorded here per MEL-ENGINE-I: audio buses/groups/effects graph,
  codec module (decode feeding pull sources, encode eating taps), cloud/
  local tts+stt providers once `http` lands, spatial audio.
