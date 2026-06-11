# speech — specification

## Objects

- `Mel_Speech_Voice` — TTS output identity (slotmap handle, generational).
- `Mel_Speech_Recognizer` — STT identity, one per language/engine (slotmap handle).
- `Mel_Speech_Utterance` — one live TTS playback (handle; dead once resolved).
- `Mel_Speech_Session` — one live STT capture (handle; dead once resolved).
- `mel_speech_auth` — open descriptor, const singletons: granted, denied,
  not_determined, restricted.

## Status

`Mel_Speech_Status` is severity (low 2 bits: OK/WARNED/ERROR) plus a bitset; never
an enum. Warning bits name fidelity losses from lowering; result bits name failure
or completion causes (DENIED, NO_DEVICE, BUSY, UNSUPPORTED, LOST, CANCELLED,
ABORTED, AUDIO, NETWORK).

## Capabilities

- `Mel_Speech_Voice_Caps`: rate (+ `rate_min`/`rate_max` as multipliers of the
  voice's native rate), pitch, volume, ranges (word-boundary marks), can_pause.
- `Mel_Speech_Recognizer_Caps`: on_device, partials, can_stop.

Caps are declared by the provider at enumerate; the core lowers every request onto
them and names each loss in warning bits. Caps never lie optimistically; an
under-claim is honest, an over-claim is a provider bug.

## Init & registry

`mel_speech_init(alloc)` — no executor, no vat: the module spawns nothing and owns
no timers (MEL-ENGINE-III); completion is provider-driven and exact. Providers
register at init via `mel_speech__register_host_providers()` (platform-selected
translation unit) or later via `mel_speech_provider_register`. `mel_speech_refresh`
re-enumerates every active provider, updates descriptors in place keyed by
`(provider, stable_id)`, and removes vanished identities; active utterances/sessions
on a removed identity resolve `ERROR|LOST`.

## Speaking

`mel_speech_speak(voice, text, opts)` returns `{ utterance, status }` synchronously.

- `text` is borrowed for the duration of the call; empty text fails loud.
- `rate`/`pitch`/`volume` are multipliers; `0` = voice native (explicit semantic).
- Lowering: rate clamped to caps range or dropped when `!caps.rate`
  (`WARN_RATE_CLAMPED`); pitch/volume dropped when uncapped (`WARN_*_DROPPED`);
  `on_range` dropped when `!caps.ranges` (`WARN_RANGES_DROPPED`).
- `on_range` delivers byte ranges into the utterance text just before each unit is
  spoken. `on_complete` fires exactly once: OK, OK|ABORTED, or ERROR|*.
- `pause`/`resume` require `caps.can_pause` and a provider entry; both absent =
  loud error, never a silent no-op. `abort` resolves OK|ABORTED immediately; a late
  provider completion for a resolved utterance is ignored (resolved guard).
- Multiple concurrent utterances per voice are permitted; the provider arbitrates
  mixing/queueing.

## Listening

`mel_speech_listen(recognizer, opts)` returns `{ session, status }` synchronously.

- `on_result` is mandatory (a session with no consumer is a bug, loud error).
- One live session per recognizer; a second `listen` fails `ERROR|BUSY`.
- `partials` lowered against `caps.partials` (`WARN_PARTIALS_DROPPED`).
- `Mel_Speech_Result.text` is borrowed, valid only during `on_result`; `final`
  marks the terminal transcript; `confidence` is 0 when the backend doesn't report.
- `mel_speech_listen_stop` is graceful: provider drains audio, delivers the final
  result, then completes. Without a provider stop it lowers to abort and returns
  `WARNED|WARN_STOP_SYNTHESIZED`. `mel_speech_listen_abort` resolves OK|ABORTED
  immediately, no final result.

## Authorization

Future-shaped, mirroring camera: `mel_speech_authorization()` is the synchronous
snapshot; `mel_speech_authorize(alloc)` returns a `Mel_Future*` resolved with a
`const mel_speech_auth*` singleton (never freed by the caller).
`mel_speech_future_free` is the module's single future destructor. STT providers
gate `listen` on their own authorization and fail `ERROR|DENIED` when unconsented;
TTS requires no consent on any current platform.

## Provider registry

`Mel_Speech_Provider_Desc` vtable: enumerate (voices, recognizers — return total
count, write up to cap; the core grows and re-calls), speak/pause/resume/abort,
authorization/authorize, listen/stop/abort, natives, shutdown. Enumerated `str8`
names/languages are provider-interned and must stay valid until the next enumerate
or shutdown. Sink callbacks carry an opaque core token; providers must deliver
each terminal callback at most once and may call from any thread.

## Threading

The core is not internally synchronized; callers drive the public API from one
thread. Provider sinks may fire on OS threads; the core's resolved guards make
late/racing terminal callbacks idempotent, and consumers marshal results to their
own executor or vat.

## Platform lowering

- Apple — AVSpeechSynthesizer (rate mapped as multiplier of
  `AVSpeechUtteranceDefaultSpeechRate`, clamped to the platform min/max; UTF-16
  boundary ranges converted to UTF-8 byte ranges); SFSpeechRecognizer +
  AVAudioEngine input tap; auth = most restrictive of speech-recognition and
  microphone consent.
- Android — TextToSpeech / SpeechRecognizer over JNI; `RECORD_AUDIO` manifest
  fragment. Sequenced.
- Win32 — WinRT SpeechSynthesizer / SpeechRecognizer. Sequenced.
- Linux — speech-dispatcher (TTS); no blessed host STT, honest-absent. Sequenced.
- Web — SpeechSynthesis / webkitSpeechRecognition. Sequenced.

## Test contract

`speech-core` proves the core against a deterministic mock provider: enumeration
and describe, auth future grant/deny, lowering warnings (rate clamp, pitch/volume/
ranges/partials drop), completion exactly-once, abort idempotence against late
provider callbacks, busy gating, graceful vs synthesized stop, refresh-loss
resolution, shutdown aborting all live work. No hardware, no audio, no network.
