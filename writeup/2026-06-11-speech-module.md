# 2026-06-11 — speech module (TTS + STT)

## Work done

New `modules/speech/`: text-to-speech and speech-to-text in the camera/vibration
mold — provider-plugin registry, slotmap handles, open `mel_speech_auth`
descriptors (no enums), severity+bitset status, option lowering against declared
caps with every fidelity loss named in warning bits, authorization as a
`Mel_Future`, borrowed-text result delivery.

- `include/speech/speech.h` — voices/recognizers/utterances/sessions, caps,
  speak/listen opts (designated-init macros mirroring `mel_vib_play`).
- `include/speech/provider.h` — vtable with both surfaces; a provider may expose
  only one (honest-absent).
- `src/speech.c` — registries with refresh + LOST resolution, lowering,
  exactly-once completion with resolved guards (late provider callbacks after
  abort are ignored), busy gating (one live session per recognizer).
- `src/apple/speech_apple.m` — AVSpeechSynthesizer TTS (rate as multiplier of the
  platform default, UTF-16 boundary ranges converted to UTF-8 byte offsets);
  SFSpeechRecognizer + AVAudioEngine STT; auth combines speech + mic consent.
- `src/speech_host_none.c` — linux/win32/android/wasm stub so the module builds
  everywhere (verified wasm).
- `test/speech_test.c` — 22 deterministic mock-provider tests, all passing
  (`./nob test speech-core`).
- `readme.md`, `spec.md`, `todo.md`.

Notable porting hazard discovered: on the case-insensitive macOS filesystem,
`#import <Speech/Speech.h>` resolves to our own `speech/speech.h` (the module's
`-I include` path wins over framework search), and `#pragma once` then suppresses
the real umbrella entirely. Fixed by importing the framework's subheaders
(`<Speech/SFSpeechRecognizer.h>`, …), which don't collide.

## Kludges

- **Speech.framework umbrella collision** — the module imports Speech subheaders
  instead of the umbrella. Any consumer TU that inherits our public include path
  and imports `<Speech/Speech.h>` will silently get our header instead. Debt: the
  collision is dodged, not removed; renaming the include subtree (cf.
  tuning→musictuning) would remove it.
- **Apple `on_device` caps reported false** — measuring it needs an
  `SFSpeechRecognizer` instance per locale at enumerate; v1 under-claims instead.
- **Apple task errors blanket-mapped to `RESULT_NETWORK`** — error-domain
  discrimination (network vs audio route) not implemented.
- **`confidence` is 0 when the backend doesn't report** — for partials and silent
  backends; only final Apple results carry a segment-averaged value.
- **Core is single-threaded while provider sinks may fire cross-thread** — same
  contract camera ships with (documented in spec §Threading); the resolved guards
  make terminal races idempotent but result delivery itself is unsynchronized.
- **Concurrent TTS utterances mix rather than queue on Apple** — one
  AVSpeechSynthesizer per utterance; the spec leaves arbitration to the provider.
- **`voice_native` returns a bridged, unretained pointer** into the framework's
  cached `speechVoices` array.

## CLAUDE.md suggestions

- Module naming: warn that a module whose public include subtree case-collides
  with an Apple framework name (Speech, Metal, GameKit, …) breaks that framework's
  umbrella import on macOS; check before naming a module.

## Suggestions

- A `hello-speech` demo app (speak a phrase, live transcript label) would
  run-verify the apple backend; plist needs `NSSpeechRecognitionUsageDescription`
  + `NSMicrophoneUsageDescription`.
- TTS synthesis-to-PCM should land together with the audio mixer
  (`design/audio-mixer-core.md`) — same lowering, one new provider entry.
- Android backend is the highest-value next platform (JNI bridge already proven by
  camera/vibration).
