# tts — specification

Text-to-speech: enumerate voices across providers, speak with rate/pitch/
volume, SSML input, word-boundary and viseme marks, pause/resume/abort,
offline render to PCM. Split from the former `speech` module: TTS shares
nothing operational with recognition — no consent, no microphone, different
lowering — so a screen-reader app links synthesis alone (and on Apple stops
linking Speech.framework). Provider-plugin shaped like `audioin`/`camera`:
host OS engines are provider 0; cloud (OpenAI, ElevenLabs) and local (piper)
providers register identically once `http`/their runtimes land.

## Objects

- `Mel_Tts_Voice` — output identity (slotmap handle, generational).
- `Mel_Tts_Utterance` — one live playback or render (handle; dead once
  resolved).

## Status

Severity (low 2 bits) + bitset, never an enum. Warning bits name lowering
losses (`RATE_CLAMPED`, `PITCH_DROPPED`, `VOLUME_DROPPED`, `RANGES_DROPPED`,
`VISEMES_DROPPED`); result bits name causes (`BUSY`, `UNSUPPORTED`, `LOST`,
`CANCELLED`, `ABORTED`, `AUDIO`, `NETWORK`).

## Capabilities

`Mel_Tts_Voice_Caps`: rate (+ `rate_min`/`rate_max` multipliers of voice
native), pitch, volume, ranges (word boundaries), can_pause, render, ssml,
visemes. Declared at enumerate; the core lowers every request onto them and
names each loss. Under-claiming is honest; over-claiming is a provider bug.

## Speaking

`mel_tts_speak(voice, text, opts)` → `{ utterance, status }` synchronously.

- `text` borrowed for the call; empty text fails loud.
- `rate`/`pitch`/`volume` multipliers; `0` = voice native (explicit
  semantic, not a hidden default).
- `opt.ssml` marks `text` as SSML. Functional, not fidelity: `!caps.ssml`
  fails `ERROR | UNSUPPORTED` — SSML is never silently read as prose.
- `on_range` delivers UTF-8 byte ranges just before each unit is spoken
  (dropped with `WARN_RANGES_DROPPED` when uncapped). `on_viseme` delivers
  `{ viseme, range }` for lip-sync; viseme ids are provider-scoped (the voice
  descriptor names its `viseme_set`); dropped with `WARN_VISEMES_DROPPED`.
- `on_complete` fires exactly once: `OK`, `OK|ABORTED`, or `ERROR|*`.
- `pause`/`resume` require `caps.can_pause` + provider entry; absence is a
  loud error, never a no-op. `abort` resolves `OK|ABORTED` immediately; late
  provider completions are ignored (resolved guard).
- Concurrent utterances per voice are permitted; the provider arbitrates.

## Rendering

`mel_tts_render(voice, text, opts)` synthesizes offline to PCM — nothing
audible. Returns `{ utterance, status }`; the utterance is abortable. Gated
by `caps.render` (`ERROR | UNSUPPORTED` — honest-absent, never a recorded
loopback). Same rate/pitch/volume lowering and warning bits as speak; `ssml`
honored identically. `on_render` is mandatory and the single terminal
callback, exactly once: `OK` with PCM, or `ERROR|*` / `OK|ABORTED` with
`pcm == NULL`. `Mel_Tts_Render.frames` is borrowed, valid only during the
callback — the module's one lifetime rule; play it by copying into
`mel_mixer_pcm_from_float` inside the callback.

## Registry & refresh

`mel_tts_init(alloc)` — spawns nothing, owns no timers (MEL-ENGINE-III).
Host providers register via `mel_tts__register_host_providers()` (platform-
selected TU); externals via `mel_tts_provider_register` anytime.
`mel_tts_refresh` re-enumerates, reconciles keyed by `(provider, stable_id)`;
vanished voices die, their live utterances resolve `ERROR | LOST`.

## Provider contract

Vtable: enumerate_voices (count-and-cap, core grows and re-calls), speak,
pause, resume, abort, render, voice_native, shutdown. Enumerated `str8`s are
provider-interned, valid until next enumerate/shutdown. Sink callbacks carry
the opaque core token, fire from any thread, each terminal at most once. NULL
`render` must pair with `caps.render == false` everywhere — over-claim is a
provider bug.

## Threading

Core caller-driven single-threaded; provider sinks fire on their threads;
resolved guards make late terminals idempotent; consumers marshal to their
own executor/vat.

## Headers

`tts/tts.h`, `tts/provider.h`. (The `speech/` umbrella and its no-speech.h
case-collision rule die with the old module; `tts.h` collides with nothing.)

## Platform lowering

- Apple — AVSpeechSynthesizer; rate as multiplier of the default rate clamped
  to platform bounds; UTF-16 ranges → UTF-8; render via the write API;
  no SSML (`caps.ssml = false`), no visemes.
- Android — TextToSpeech via `platform` JNI + java helper; pause
  honest-absent; render via synthesizeToFile→PCM where offered; SSML subset
  honest per engine.
- Win32 — SAPI 5 ISpVoice; log-scale rate; word boundaries; visemes
  first-class (SAPI viseme events); SSML native; render via stream binding;
  abort lowers to queue purge (every purged utterance resolves ABORTED).
- Linux — speech-dispatcher over SSIP; SSML mode for ranges;
  honest-absent without the daemon; no visemes, no render.
- Web — speechSynthesis; boundary events → UTF-8; SSML where the engine
  accepts it (declared per voice honestly); no visemes, no render.

## Dependencies

- `core`, `allocator`, `collection` (registry slotmaps), `string` (`str8`),
  `log`, `thread` (provider waiter threads on some hosts);
  `platform` (android only). No `future` (nothing async-shaped here),
  no consent, no audio modules — render hands PCM to the caller.

## Test contract

`tts-core` against a mock provider: enumeration/describe, lowering warnings
(rate clamp, pitch/volume/ranges/visemes drops), ssml/render unsupported loud
failures, speak/render completion exactly-once, abort idempotence, refresh-
loss resolution, render borrow rule, shutdown aborting live work. No
hardware, no audio.
