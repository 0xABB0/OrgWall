# speech

OS speech services: text-to-speech (enumerate voices, speak with rate/pitch/volume,
word-boundary marks, pause/resume/abort) and speech-to-text (enumerate per-language
recognizers, authorize, stream partial and final transcripts from the microphone).

Structurally this is `camera` (authorization future, provider-plugin backends,
registry refresh) crossed with `vibration` (option lowering against declared caps,
completion callbacks, playback-style handles).

## Result lifetime — the load-bearing contract

`Mel_Speech_Result.text` borrows the recognizer's transcript buffer and is valid
**only for the duration of the `on_result` callback**. A consumer that needs the
text afterward must copy it inside the callback. The same borrow rule applies to
the `text` passed into `mel_speech_speak`: providers consume it during the call;
it is not retained.

Providers may invoke result/completion callbacks on their own threads (the same
contract as camera frame delivery); consumers marshal to their own executor or
vat if they need to.

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

## Provider plugin

Backends register a `Mel_Speech_Provider_Desc`; the core owns handles, registries,
and lowering. A provider may expose only one of the two surfaces — a TTS-only
provider leaves the listen entries `NULL` and recognizers simply don't appear
(honest-absent, never faked).

## Platforms

macOS/iOS (`src/apple/speech_apple.m`): AVSpeechSynthesizer for TTS,
SFSpeechRecognizer + AVAudioEngine for STT. STT authorization combines speech
recognition and microphone consent and answers the most restrictive of the two.
Apps must carry `NSSpeechRecognitionUsageDescription` and
`NSMicrophoneUsageDescription` in their Info.plist partial (and the
`com.apple.security.device.audio-input` entitlement when sandboxed). Android
TextToSpeech/SpeechRecognizer, Win32 WinRT SpeechSynthesizer/SpeechRecognizer,
Linux speech-dispatcher, and wasm Web Speech are sequenced; those platforms build
the `host_none` stub so the module always links (MEL-ENGINE-I/VII).

Headless TTS/STT cannot be run-verified; module logic is proven against a mock
provider (`test/speech_test.c`).

## Dependencies

`core`, `allocator`, `collection`, `future`, `executor`, `string`, `log`.

## Test

`./nob test speech-core` — mock provider, deterministic, no hardware, no audio.
