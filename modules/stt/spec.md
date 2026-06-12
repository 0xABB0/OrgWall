# stt — specification

Speech-to-text: enumerate per-language recognizers across providers,
authorize, stream partial and final transcripts. Split from the former
`speech` module: recognition's audio story is its own — a session listens to
the OS default path, to a chosen `Mel_AudioIn` (any provider, virtual
included), or to PCM the app feeds — and none of that concerns synthesis.
Provider-plugin shaped: host OS engines are provider 0; whisper.cpp/cloud
providers register identically when their runtimes land.

## Objects

- `Mel_Stt_Recognizer` — identity, one per language/engine (slotmap handle).
- `Mel_Stt_Session` — one live recognition (handle; dead once resolved).
- `mel_stt_auth` — open descriptor singletons: granted / denied /
  not_determined / restricted.

## Status

Severity + bitset. Warning bits: `PARTIALS_DROPPED`, `STOP_SYNTHESIZED`,
`VOCABULARY_DROPPED`, `PUNCTUATION_DROPPED`, `PROFANITY_DROPPED`. Result
bits: `DENIED`, `NO_DEVICE`, `BUSY`, `UNSUPPORTED`, `LOST`, `CANCELLED`,
`ABORTED`, `AUDIO`, `NETWORK`.

## Capabilities

`Mel_Stt_Recognizer_Caps`: on_device, require_on_device (can force it),
partials, can_stop, feed, device_select, vocabulary, punctuation,
profanity_filter. Same caps law as everywhere: lowering names every loss;
over-claim is a provider bug.

## Audio sources — the three doors

`Mel_Stt_Listen_Opt` selects exactly one:

1. **OS default** — `device = MEL_AUDIOIN_NULL`, `feed = false`: the provider
   captures as the OS pleases (today's host behavior).
2. **Chosen device** — `device` set: gated by `caps.device_select`
   (`ERROR | UNSUPPORTED` when absent — never a silent fall-back to default,
   MEL-CODE-007). Virtual `audioin` devices ride this door: recognition over
   a network feed or test fixture with zero special casing (MEL-ENGINE-IX).
3. **Fed PCM** — `feed = true` + `feed_sample_rate` (zero is loud): the app
   pumps mono f32 via `mel_stt_feed(session, frames, n)`; gated by
   `caps.feed`. Frames borrowed per call. `feed` and `device` together is a
   loud error. Fed sessions never touch the microphone and skip consent.

## Listening

`mel_stt_listen(recognizer, opts)` → `{ session, status }` synchronously.

- `on_result` mandatory (a session with no consumer is a bug — loud).
- One live session per recognizer; second `listen` fails `ERROR | BUSY`.
- `partials` lowered against caps (`WARN_PARTIALS_DROPPED`).
- Tuning, each caps-gated and lowered loudly: `vocabulary` (contextual
  phrase biasing, `str8` array borrowed for the call), `punctuation`
  (automatic punctuation), `profanity_filter`, `require_on_device`
  (functional: `ERROR | UNSUPPORTED` when the engine cannot guarantee
  on-device — privacy promises are never best-effort).
- `Mel_Stt_Result.text` borrowed, valid only during `on_result`; `final`
  marks terminal transcripts; `confidence` 0 when unreported.
- `mel_stt_stop` graceful (drain → final result → complete); lowers to abort
  with `WARNED | WARN_STOP_SYNTHESIZED` when the provider can't drain.
  `mel_stt_abort` resolves `OK | ABORTED` immediately. `on_complete` exactly
  once; resolved guards swallow late provider terminals.

## Authorization

Future-shaped: `mel_stt_authorization()` snapshot; `mel_stt_authorize(alloc)`
→ `Mel_Future*` → `const mel_stt_auth*` singleton; `future_auth` /
`future_free`. The answer composes speech-recognition consent with
`audioin`'s microphone consent (most restrictive) — providers answer for
recognition alone and carry no microphone-consent code. Door 3 (fed) needs no
consent at all; doors 1–2 fail `ERROR | DENIED` unconsented.

## Registry & refresh

`mel_stt_init(alloc)` — spawns nothing, owns nothing periodic
(MEL-ENGINE-III). Host providers via `mel_stt__register_host_providers()`;
externals via `mel_stt_provider_register`. `mel_stt_refresh` reconciles by
`(provider, stable_id)`; sessions on vanished recognizers resolve
`ERROR | LOST`.

## Provider contract

Vtable: enumerate_recognizers, authorization/authorize (recognition consent
only), listen (lowered opts carry the door: device stable id or feed format),
stop, abort, feed, recognizer_native, shutdown. Interned `str8`s valid until
next enumerate/shutdown; sink terminals at most once, any thread. NULL
`feed`/device support must pair with honest caps.

## Threading

Core caller-driven single-threaded; provider sinks on their threads;
`mel_stt_feed` is callable from one producer thread per session, wait-free
into the provider.

## Platform lowering

- Apple — SFSpeechRecognizer; buffer-append path serves doors 2–3
  (`caps.feed = true`, `device_select` honest per route capability);
  contextualStrings vocabulary; `requiresOnDeviceRecognition` for
  require_on_device; auth = SFSpeech consent × `audioin` consent.
- Android — SpeechRecognizer via `platform` JNI: no feed, no device select
  (honest-absent), biasing strings where the engine offers them;
  RECORD_AUDIO via `audioin`.
- Win32 — SAPI dictation: hypothesis = partial; feed via custom audio stream
  object (`caps.feed = true`); no vocabulary (honest), stop completes without
  drain (synthesized, warned).
- Linux — no blessed host STT: empty enumeration, honest; whisper.cpp
  provider is the intended path.
- Web — webkit SpeechRecognition: navigator language only, no feed/device
  select; consent via getUserMedia composition.

## Dependencies

- `core`, `allocator`, `collection`, `string`, `log`, `thread`.
- `future` — the consent future.
- `audioin` — consent composition and `Mel_AudioIn` device-door identity.
- `platform` (android only) — JNI bridge.

## Test contract

`stt-core` against a mock provider: enumeration/describe, composed-auth
most-restrictive grant/deny, three-door gating (device-select unsupported
loud, feed format violations loud, feed+device loud), vocabulary/punctuation/
profanity lowering warnings, require-on-device hard failure, partials drop,
busy gating, graceful vs synthesized stop, feed-after-terminal rejection,
abort idempotence, refresh-loss, shutdown. No hardware, no network.
