# speech

OS speech services: text-to-speech (enumerate voices, speak with rate/pitch/volume,
word-boundary marks, pause/resume/abort) and speech-to-text (enumerate per-language
recognizers, authorize, stream partial and final transcripts from the microphone).

Structurally this is `camera` (authorization future, provider-plugin backends,
registry refresh) crossed with `vibration` (option lowering against declared caps,
completion callbacks, playback-style handles).

## Headers

- `<speech/tts.h>` — voices, speak, utterances.
- `<speech/stt.h>` — recognizers, listen, sessions.
- `<speech/common.h>` — status, authorization, init/refresh (pulled in by both).
- `<speech/provider.h>` — the backend surface.

There is deliberately no `speech/speech.h`: on case-insensitive filesystems that
file would shadow Apple's `<Speech/Speech.h>` umbrella (the module's public include
path is searched before framework paths), silently breaking every TU that imports
the framework. Never add one.

## Result lifetime — the load-bearing contract

`Mel_Speech_Result.text` borrows the recognizer's transcript buffer and is valid
**only for the duration of the `on_result` callback**. A consumer that needs the
text afterward must copy it inside the callback. The same borrow rule applies to
the `text` passed into `mel_speech_speak`: providers consume it during the call;
it is not retained.

Providers may invoke result/completion callbacks on their own threads (the same
contract as camera frame delivery); consumers marshal to their own executor or
vat if they need to (`apps/hello-speech` shows the dirty-flag + vat-tick pattern).

## Open descriptors (not enums)

`mel_speech_auth` is open data with const singletons (`mel_speech_auth_granted`, …)
— authorization states are classifications that grow, not protocols (MEL-CODE-001).

## Lowering

`mel_speech_speak` lowers options onto the voice's declared `Mel_Speech_Voice_Caps`,
naming every fidelity loss in the returned status' warning bits: rate clamped into
`[rate_min, rate_max]` or dropped, pitch/volume dropped, ranges dropped. A rate,
pitch, or volume of `0` means "voice native" — explicit semantics, not a hidden
default. `mel_speech_listen` lowers `partials` the same way; `mel_speech_listen_stop`
falls back to abort (named `MEL_SPEECH_WARN_STOP_SYNTHESIZED`) when the provider
cannot drain a final result.

## Futures

`mel_speech_authorize` returns a `Mel_Future*` resolving to a
`const mel_speech_auth*` (read via `mel_speech_future_auth`); that pointer is a
static singleton and must never be freed. `mel_speech_future_free` is the one
destructor for futures this module returns.

## Provider plugin — composition

Backends register a `Mel_Speech_Provider_Desc`; the core owns handles, registries,
and lowering. A provider may expose only one of the two surfaces — a TTS-only
provider leaves the listen entries `NULL` and recognizers simply don't appear
(honest-absent, never faked). Host backends are not special: a cloud TTS/STT
provider (OpenAI, ElevenLabs), a local model (whisper.cpp, piper), or a game's own
synthesizer composes through the exact same `mel_speech_provider_register` call and
shows up in the same voice/recognizer lists (MEL-ENGINE-IX). Cloud providers wait
on an `http` module; nothing in this surface will need to change for them.

## Platforms — all five hosts implemented

- macOS/iOS (`apple/src/speech_apple.m`) — AVSpeechSynthesizer; SFSpeechRecognizer
  + AVAudioEngine. STT authorization combines speech-recognition and microphone
  consent and answers the most restrictive. Apps carry
  `NSSpeechRecognitionUsageDescription` + `NSMicrophoneUsageDescription` in their
  plist partial (and `com.apple.security.device.audio-input` when sandboxed).
- Android (`android/src/`) — `TextToSpeech` + `SpeechRecognizer` through the
  `platform` JNI bridge and a `MelodySpeech` java helper; ships the `RECORD_AUDIO`
  manifest fragment. Voices appear once the engine finishes its async init — call
  `mel_speech_refresh` (the app's Refresh button exists for this).
- Win32 (`win32/src/speech_sapi.c`) — SAPI 5: `ISpVoice` (rate mapped log-scale to
  ±10, word boundaries converted UTF-16→UTF-8), shared `ISpRecognizer` dictation
  with hypothesis partials; completion via notify-event waiter threads.
- Linux (`linux/src/speech_spd.c`) — speech-dispatcher over its SSIP unix socket
  directly (no libspeechd dependency), notifications drive exact completion;
  honest-absent when the daemon isn't running. No blessed host STT.
- Web (`web/src/speech_web.c`) — `speechSynthesis` + `SpeechRecognition`
  (webkit-prefixed) via EM_JS; microphone consent through `getUserMedia`.

Showcase: `apps/hello-speech` (macos/ios/android/wasm/win32) — voice cycler,
rate slider, speak/pause/resume/abort, authorize, live transcript.

## Dependencies

`core`, `allocator`, `collection`, `future`, `executor`, `string`, `log`,
`thread`; `platform` on Android.

## Test

`./nob test speech-core` — mock provider, deterministic, no hardware, no audio.
