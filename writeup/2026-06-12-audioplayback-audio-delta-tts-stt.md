# 2026-06-12 — audioplayback, the audio delta, tts & stt

## Work done

The remaining audio-v2 modules, in the order the previous session named:
audioplayback first (the audio delta consumes it), then the audio delta,
then tts and stt in parallel. The legacy `speech` module is retired.

### Output negotiation (contract round, audioout)

The pull plane had no door for what audioplayback's wireframe demands
(exclusive, latency, os_timestamps), so provider `open` grew the exact
output twin of the capture-negotiation round:
`Mel_AudioOut_Open_Opt {exclusive}` +
`Mel_AudioOut_Granted {format, exclusive, os_timestamps, latency_frames}`
(granted format folded into Granted). All six host backends honor it from
OS read-back, never the request echoed:

- macos — hog-mode exclusive (audioin's idiom verbatim: take, read back
  owner pid; foreign hog on open failure → BUSY), latency = device latency
  + buffer frames + stream latency from the HAL.
- win32 — full WASAPI exclusive render path (IsFormatSupported exclusive →
  Initialize event-driven → AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED aligned-buffer
  retry, pcm16 probe per the capture twin); exclusive render thread writes
  exactly buffer_frames per event; latency = GetStreamLatency +
  GetBufferSize. Compile-unverified (box offline, again).
- android — AAUDIO_SHARING_MODE_EXCLUSIVE requested, getSharingMode read
  back (AAudio silently lowers); latency = getBufferSizeInFrames.
- linux — ALSA hw-device opens grant exclusive (kernel single-owner),
  `alsa:default` grants shared; latency = snd_pcm_get_params buffer_size.
- ios/web — exclusive honest-absent; latency from AVAudioSession
  (outputLatency + IOBufferDuration) / AudioContext baseLatency+outputLatency.

### audioplayback (implements the wireframe)

`src/audioplayback.c`: write mode (pcm SPSC ring; provider-pull drains it;
empty ring pads silence, every padded frame counted +
sticky-until-next-write WARN_UNDERRUN) XOR pull mode (provider pull lands
in the caller's fn through one trampoline — zero copies when formats
match). Granted-format mismatches convert inside: streaming linear
resample with one-sample history (continuous across pulls) + remix +
interleave, WARN_CONVERTED at open. Exclusive lowering honest
(WARN_EXCLUSIVE_DROPPED + granted read-back); BUSY/UNSUPPORTED/NO_DEVICE
mapping; sticky LOST rejects writes. latency = granted device latency
rescaled to stream rate + ring fill. 10 mock-provider tests, including a
tracking-allocator round-trip.

### audio delta (device binding, taps, pull source)

- `backend.h` and the five compile-time platform backends are deleted —
  the engine's device plane is an `audioplayback` pull-mode client; every
  open routes through the audioout registry (virtual sinks ride free).
- `MEL_AUDIOOUT_NULL` follows the system default and migrates on
  `default_changed` (fires `{default_changed}` + `{format_changed}` when
  the new route's native format differs). A pinned device that vanishes
  holds the engine — ring fills, mix thread sleeps on its sem, clock stops
  naturally — `ERROR | DEVICE_LOST` until `set_device`.
- `set_device` rebinds live: new stream opens before the old closes;
  voices/faders/commands untouched; failure keeps the previous binding.
- Interruptions: audio now depends on audiopolicy and subscribes to its
  events when `mel_audiopolicy_active()` (new one-line query). began →
  hold + `{interrupted}` + `ERROR | INTERRUPTED` (new status bit);
  ended+should_resume → reopen + `{resumed}`. Initialize audiopolicy
  before `mel_audio_create` to get these — recorded in spec.md.
- Taps: post-master and per-voice post-fader, pcm rings written by the mix
  thread; attach/detach ride the command queue (mix thread owns the list);
  drops counted; a voice tap drains after its voice dies. Tap scratch is
  allocated on first attach and regrown by the offline path only while
  taps exist (MEL-ENGINE-III).
- Pull source: interleaved pull fn → planar source; short reads pad
  silence and the voice stays live; zero channels/rate refused loudly;
  single-instance enforced through a new optional `instance_open` hook in
  the source vtable (play refuses the second voice; instance_free
  releases).
- `mel_audio_device_events` payload is the real `Mel_Audio_Device_Event`
  (was a u32 placeholder).
- Tests: audio-device (7, hermetic: compiles audioout/audioplayback/
  audiopolicy core sources directly with a mock provider + mock policy
  backend — fork-safe, no CoreAudio) and audio-taps (7, offline). The 7
  pre-existing audio suites — broken since the wireframe commit pointed
  engine.h at pcm/audioout without build deps — compile again and pass.

### tts (implements the wireframe; supersedes speech's synthesis half)

Core (19 mock tests green): provider registry, voice slotmap keyed
(provider, stable_id), caps lowering naming every loss (rate clamp to
rate_min/max, pitch/volume/ranges/visemes drops), SSML/render functional
gating (never silently lowered), atomic resolved guards (terminals
exactly once from any thread), refresh-loss → ERROR|LOST, shutdown aborts
live work. Host providers, all five platforms (ported from the legacy
speech module's working idioms onto the new vtable):

- apple — AVSpeechSynthesizer; per-utterance synthesizer instances so
  pause is effectively per-utterance; honest rate bounds from the platform
  constants; UTF-16→UTF-8 ranges; render via writeUtterance (f32/i16/i32,
  interleaved+planar).
- win32 — SAPI 5; log-scale rate/pitch mappings; word boundaries with a
  spoken→original index map (escaping/wrapping accounted); visemes
  first-class (viseme_set "sapi"); native SSML; render via memory-stream
  binding; abort = queue purge, every purged utterance resolves ABORTED.
- android — TextToSpeech over platform JNI + java helper; pause
  honest-absent; render via synthesizeToFile + WAV parse (incl. extensible
  formats); API-26 ranges gate; async engine init → first enumeration is
  honestly empty, refresh after ready.
- linux — speech-dispatcher over raw SSIP (no daemon → zero voices,
  reconnect on refresh); ranges/ssml declared absent (legacy never wired
  index marks — ported honestly).
- web — speechSynthesis with the audioin-style enumerate-cache +
  voiceschanged; boundary → UTF-8; pause queue-global (noted); abort =
  cancel-purge.

### stt (implements the wireframe; supersedes speech's recognition half)

Core (19 mock tests green): three audio doors (OS default / chosen
Mel_AudioIn gated by caps.device_select / fed PCM skipping consent),
composed authorization (provider recognition consent × audioin mic
consent, most restrictive, future-shaped), graceful vs synthesized stop,
busy gating, feed-after-terminal rejection, refresh-loss, shutdown. Host
providers:

- apple — SFSpeechRecognizer per locale; on-device caps from per-locale
  read-back; device door implemented on macOS (CoreAudio device bound to
  the AVAudioEngine input unit), honest-absent on iOS; fed door via
  buffer-append; contextualStrings; punctuation gated at runtime.
- win32 — SAPI dictation over CLSID_SpInprocRecognizer (deviation from
  legacy's shared recognizer — required for SetInput); fed door through a
  custom C-COM ISpStreamFormat whose Read blocks on app-fed PCM;
  can_stop=false so the core synthesizes stop (warned) per spec.
- android — SpeechRecognizer via JNI + java helper + manifest queries
  fragment; on-device caps via the API-33 checkRecognitionSupport probe
  (false until confirmed); no feed/device select (honest).
- web — webkitSpeechRecognition; one recognizer for navigator.language;
  no-speech resolves OK with no fabricated empty final.
- linux — honest-absent stub (whisper.cpp is the intended path).

### speech retired

`modules/speech/` deleted; `apps/hello-speech` ported to tts + stt
(audioin initialized for the consent composition). Specs' guidance
honored: the split modules carry the full surface.

### Fixed on the way

- **allocator/tracking**: the free path forwarded `align = 0` to the
  backing allocator, sending every aligned allocation down the heap's
  unaligned free — libmalloc abort. Align is now threaded through.
  Existing tracking tests stayed green; any aligned-alloc user routed
  through a tracking allocator was affected.

### Verification

- macos: full `./nob test` run; all audio-family suites green
  (pcm 20, spectrum 11, audioin 14, audioout 9, audiopolicy 9,
  audiocapture 11, audioplayback 10, tts 19, stt 19, audio 46 across 8
  suites). gpu-resources/metal/scene still crash under the forked runner
  (fork + ObjC initializer, pre-existing, unrelated).
- Cross-compiles clean from this host: audioout, audioplayback, audio,
  tts, stt × {ios, android, wasm, linux}; hello-speech links+packages on
  macos.
- win32: all twelve+ touched win32 TUs (audioout wasapi exclusive, tts
  sapi, stt sapi) are committed but **not compiled** — win-pilot
  unreachable all session. First action when it returns:
  `git pull` + `nob build audioout audioplayback audio tts stt`.

## Kludges

- audioplayback conversion scratch grows on the provider (RT) thread —
  amortized (first pull and format changes only), same confession as
  audiocapture; a worst-case bound the contract doesn't have would fix it.
- Tap overruns drop NEWEST frames (pcm SPSC, producer can't drop-oldest);
  spec says oldest. Same standing deviation as audiocapture's ring —
  Gabbo's call on a pcm producer-overwrite mode resolves both.
- Pull-source scratch also grows on the mix thread (amortized; instance
  alloc'd buffers).
- The engine learns device death via hotplug `removed` (refresh-driven),
  not the pull plane's immediate `on_lost` — audioplayback consumes that
  callback for its own sticky status and exposes no loss callback. A
  status-change callback on audioplayback would close the gap; wireframe
  question.
- `mel_audio_device_status` during follow-mode migration failure reports
  DEVICE_LOST while `bound` still names the vanished device — honest
  enough but the handle is dead; rebind clears it.
- audio depends on audiopolicy now (link-level) purely for interruption
  events; subscription happens only if audiopolicy was initialized before
  the engine — an init-order sensitivity, documented in spec.md, not
  detectable at compile time.
- tts/stt host providers are compile-verified only (macos runs the apple
  TUs; ios/android/wasm/linux cross-compile); no hardware/manual smoke of
  actual synthesis/recognition ran this session.
- tts apple/web stable_ids are content hashes (NSString hash / str8_hash
  of voiceURI) — stable within a process, not across OS sessions for the
  NSString case; fine for refresh reconciliation, named here.
- tts android first enumeration is empty until the async engine init
  completes; tts has no provider-notify surface (audioin has one), so the
  app must refresh after ready. Wireframe question: grow
  `mel_tts_provider_notify`.
- stt win32 swapped the legacy shared recognizer for an in-proc one
  (SetInput requires it); behavior difference (no OS-shared dictation
  profile UI) accepted and documented in the agent report.
- The fork()+ObjC gpu test crashes and the per-invocation mpfr/gmp
  rebuild churn predate this session and remain.
- Four foreign modified files (collection/mpmc.h, mpsc.h,
  core/platform.h, log/src/log.c — concurrent session's cache-line work)
  remain uncommitted on the branch, deliberately untouched.

## CLAUDE.md suggestions (recommendations only)

- None new.

## Suggestions

- win-pilot first action when reachable: `git pull` +
  `nob build audioout audioplayback audio tts stt` (three new win32 COM
  TUs plus the exclusive render path are unverified).
- Parked wireframe questions, one round: pcm producer-overwrite ring mode
  (drop-oldest for capture overruns and tap drops), audioplayback loss
  callback (engine-immediate death signal), `mel_tts_provider_notify`
  (web voiceschanged + android async init), web capture granted-update
  event (carried over from last session).
- PipeWire vendoring decision still parked (linux loopback/monitors,
  publish OS-visibility).
- Cloud/local tts+stt providers (OpenAI, ElevenLabs, whisper.cpp, piper)
  once `http`/runtimes land — the provider vtables already suffice; the
  legacy speech todo's other items (per-message abort paths, voice
  selection helpers) carry over to the new modules.
- The mpfr/gmp third-party build re-runs its configure/compile on every
  nob test invocation — worth caching properly; it dominates test wall
  time.
- hello-audio app still demos the mixer; a hello that drives capture →
  pull source (karaoke loop) would smoke the whole v2 chain on hardware.
