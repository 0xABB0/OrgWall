# 2026-06-11 — speech module (TTS + STT), all host backends, hello-speech

## Work done

New `modules/speech/`: text-to-speech and speech-to-text in the camera/vibration
mold — provider-plugin registry, slotmap handles, open `mel_speech_auth`
descriptors (no enums), severity+bitset status, option lowering against declared
caps with every fidelity loss named in warning bits, authorization as a
`Mel_Future`, borrowed-text result delivery.

- Public headers split as `speech/common.h` + `speech/tts.h` + `speech/stt.h` +
  `speech/provider.h`. There is intentionally **no** `speech/speech.h`: on the
  case-insensitive macOS filesystem that file shadows Apple's
  `<Speech/Speech.h>` umbrella through the module's public `-I` path, and
  `#pragma once` then suppresses the framework entirely. The first cut hit this
  for real; the split removes the collision and the backend imports the umbrella
  normally.
- `src/speech.c` — registries with refresh + LOST resolution, lowering,
  exactly-once completion with resolved guards (late provider callbacks after
  abort are ignored), busy gating (one live session per recognizer).
- Backends for **all five hosts**:
  - apple (`speech_apple.m`) — AVSpeechSynthesizer + SFSpeechRecognizer/
    AVAudioEngine; auth = most restrictive of speech + mic consent.
  - web (`speech_web.c`) — speechSynthesis + webkit SpeechRecognition via EM_JS,
    camera_web-style token callbacks; UTF-16→UTF-8 boundary conversion in JS.
  - android (`speech_android.c` + `MelodySpeech.java`) — TextToSpeech +
    SpeechRecognizer on the main looper, RegisterNatives callbacks, RECORD_AUDIO
    manifest, permission flow through the `platform` bridge.
  - win32 (`speech_sapi.c`) — SAPI 5 in C COM: ISpVoice + shared ISpRecognizer
    dictation, notify-event waiter threads (callback notification needs a message
    pump we don't have), log-scale rate, UTF-16→UTF-8 boundary mapping.
  - linux (`speech_spd.c`) — direct SSIP client to speech-dispatcher's unix
    socket (no libspeechd link/dlopen), reader thread routes responses vs 7xx
    notifications; honest-absent without the daemon.
- `apps/hello-speech` — showcase: voice cycler + refresh, rate slider, speak/
  pause/resume/abort with word-range updates, authorize future polled on a vat
  tick, listen toggle with live partial/final transcript. Cross-thread provider
  callbacks marshal into the GUI via dirty buffers + a 50 ms vat tick.
- 22 mock-provider tests (`./nob test speech-core`).

Verification matrix:
- macOS — lib + app build, app smoke-run (alive 3 s; enumerated 180 voices,
  63 recognizers), tests 22/22.
- iOS — app builds + packages (simulator arch).
- Android — APK builds end-to-end (gradle compiled MelodySpeech.java).
- wasm — app links; headless-chromium smoke boots the app and speech init logs
  `voices=0 recognizers=1` (honest: headless has no TTS voices, has the
  recognition API).
- win32 (remote win-pilot) — speech lib compiles, tests 22/22 pass.
- linux — lib cross-compiles from the mac host; not run (no runner).

Composition: cloud/local providers (OpenAI, ElevenLabs, whisper.cpp, piper)
register the same `Mel_Speech_Provider_Desc` from outside the module; blocked
only on an `http` module for the cloud ones, by design no module change needed.

## Kludges

- **Per-utterance abort is a global purge** on win32 (SAPI purge), web
  (`speechSynthesis.cancel`), android (`tts.stop`), linux (`CANCEL self`): those
  OS surfaces have no per-message cancel. The backends resolve every live
  utterance ABORTED (named, not silent), but it's coarser than the API shape.
- **Win32 graceful stop doesn't drain**: `listen_stop` deactivates dictation and
  completes immediately; a final hypothesis mid-flight is dropped.
- **Apple `on_device` caps reported false**, task errors blanket-mapped to
  `RESULT_NETWORK` (no error-domain discrimination).
- **Web recognizer is the navigator language only** — the API exposes no locale
  enumeration; STT is chromium/webkit-prefixed only.
- **Android voices need a manual `mel_speech_refresh`** after the engine's async
  init (no hotplug event yet); TTS pause honest-absent.
- **Linux rate/pitch mapping is linear** onto SSIP's ±100 scale — perceptually
  approximate; ranges need SSML index marks (not implemented).
- **Core is single-threaded while provider sinks fire cross-thread** — camera's
  contract, documented in spec §Threading; hello-speech shows the marshaling
  pattern.
- **`confidence` is 0 where backends don't report** (partials, speechd, android
  partials).
- **win32 hello-speech app build blocked by pre-existing gmp autotools breakage**
  on the build box ('fail'/'eval' not recognized under cmd make) — hits every
  app there (hello-vibration fails identically); module + tests verified instead.

## CLAUDE.md suggestions

- Module naming: a module whose public include subtree case-collides with an
  Apple framework name (Speech, Metal, GameKit, …) silently breaks that
  framework's umbrella import on macOS. Worth a line in the build docs; the
  speech module dodges it by never shipping `speech/speech.h`.
- The win32 box's third-party autotools (gmp) is broken under cmd's make —
  worth fixing once, it currently blocks every app build there.

## Suggestions

- TTS synthesis-to-PCM should land with the audio mixer — same lowering, one new
  provider entry.
- A whisper.cpp/piper provider module would prove the composition story without
  waiting on `http`.
- An `http` module unlocks OpenAI/ElevenLabs providers.
- Boot entries for linux/win32/android-native would let hello-speech run
  everywhere its libraries already build.
