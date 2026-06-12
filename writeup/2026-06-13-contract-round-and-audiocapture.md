# 2026-06-13 — contract round (Gabbo's answers) + audiocapture

## Work done

Gabbo answered the parked contract questions; all approved changes landed,
then `audiocapture` was implemented on top.

### Contract changes (all twelve platform backends updated in place)

- **Stream bridge promoted**: `mel_audioin__open`/`__close` moved from the
  internal header into `<audioin/provider.h>`; audioout's
  `__open/__start/__stop/__close` promoted symmetrically (audioplayback and
  the audio delta will consume them).
- **BUSY bits**: `MEL_AUDIOIN_RESULT_BUSY` and `MEL_AUDIOOUT_RESULT_BUSY`.
  Mapped: ALSA `-EBUSY` (both directions), WASAPI `AUDCLNT_E_DEVICE_IN_USE`,
  AAudio `NO_FREE_HANDLES`/`UNAVAILABLE`, iOS session-busy activation errors,
  macOS foreign hog owner.
- **Pull-plane loss**: audioout `open` takes `Mel_AudioOut_Source
  {pull, on_lost, token}`; every backend fires `on_lost` at most once per
  open on device death (DeviceIsAlive, DEVICE_INVALIDATED, route-gone,
  DISCONNECTED, setSinkId rejection, ALSA fatal write).
- **caps.mute split** (audioout): ALSA cards with volume but no switch now
  report honestly; WASAPI grants both together (one interface); apple/
  android/web false.
- **Unconditional shadow diff**: external volume changes fire `changed`
  even where volume is read-only (iOS hardware volume now observable
  end-to-end via KVO → notify → shadow diff).
- **audiopolicy**: `duck_ended` event bit (iOS secondary-audio End, Android
  GAIN); the core observes its own stream — permanent `focus_lost` releases
  `focus_held`, `focus_gained` re-arms it.
- **Capture negotiation (consequential change)**: audioin provider `open`
  grew `Mel_AudioIn_Open_Opt {processing{aec,ns,agc}, exclusive}` +
  `Mel_AudioIn_Granted {processing, exclusive, os_timestamps}`, and
  `on_frames` carries `timestamp_ns` (OS-monotonic, 0 = unknown) — the
  audiocapture spec demanded voice-processing/exclusive negotiation and OS
  capture stamps, and the provider plane had no door for either. Granted is
  always read back from what is in effect, never the request echoed.
  Per-platform honoring: macOS VPIO (AEC+NS bundled, AGC by property
  read-back) + hog-mode exclusive + mHostTime stamps; iOS
  setVoiceProcessingEnabled (Apple bundles all three) + AVAudioTime stamps;
  win32 exclusive-mode path with the aligned-buffer retry + QPC stamps,
  processing honestly lowered (APO is driver-owned); ALSA hw-exclusive
  semantics + driver htstamp (monotonic-verified, refused if wall-clock);
  android VOICE_COMMUNICATION preset (dlsym-gated, API 28+) +
  exclusive read-back + getTimestamp-derived stamps; wasm getUserMedia
  constraints with track.getSettings() truth (async — sync granted is the
  honest all-false floor), no stamps (context clock is not OS-monotonic;
  refused to fake).

### audiocapture (rework, replaces the legacy module)

- Legacy `src/ring.c`, internal header, and the macos backend deleted —
  capture IO now lives behind audioin's provider plane; pcm owns the ring.
- `src/audiocapture.c` — pure common code, no platform sources: provider
  push → format tracking (mid-stream format changes handled) → conversion
  (deinterleave with one-sample streaming history so linear resampling is
  continuous across batches, cursor starts at the first real sample;
  remix mono↔N/average-down/modulo-copy; interleave) → pcm SPSC ring.
  Wait-free consumer: `read`/`read_ex`/`available`/`status`/
  `dropped_frames` are atomics + SPSC ops.
- Timestamps: a small SPSC stamp FIFO (entries {stamp, frames}, capacity
  min(512, ring)) maps batches to ring positions; `read_ex` returns the
  first frame's stamp, partially-consumed entries offset by frames/rate;
  FIFO overflow degrades to extrapolation. Provider stamp 0 → arrival time
  (`mel_nanos_since_unspecified_epoch`), `granted.os_timestamps` says which.
- Honesty: `WARN_CONVERTED` at open from descriptor-vs-requested;
  `WARN_PROCESSING_DROPPED`/`WARN_EXCLUSIVE_DROPPED` from granted deltas;
  overrun drops counted + sticky-until-read `WARN_OVERRUN`; `on_lost` →
  sticky `ERROR | LOST`, buffered frames keep draining; open never prompts
  (DENIED without granted consent); provider BUSY surfaces as BUSY.
- 11 tests against a mock audioin provider: consent/dead-handle/busy
  gating, native passthrough, stereo→mono downmix values, 24k→48k
  resampling continuity across four batches (monotonic, slope-correct),
  processing/exclusive honesty + granted readback, overrun count +
  clear-on-read, sticky LOST drain, timestamp advance across partial reads,
  arrival fallback.

### Verification

All 8 test suites pass (74 tests: pcm 20, spectrum 11, audioin 14,
audioout 9, audiopolicy 9, audiocapture 11). All four audio modules build
on macos/ios/android/wasm/linux (20/20 combinations clean). win32 still
compile-unverified — box offline all session; both wasapi twins +
audiopolicy win32 + audiocapture (common-only) queued for it.

## Kludges

- **Spec deviation: overrun drops NEWEST frames, spec says oldest.** The
  pcm SPSC ring cannot drop-oldest from the producer side without breaking
  single-consumer ownership. Honest counting + sticky warn regardless.
  Options if oldest matters: a producer-overwrite ring mode in pcm, or
  consumer-cooperative skip. Needs Gabbo's call; spec not silently edited.
- Conversion scratch grows on the provider thread (amortized: first batch
  and format changes only; steady state allocation-free). True RT-hygiene
  would preallocate from a worst-case bound the contract doesn't have.
- The stamp FIFO caps at 512 entries with extrapolation degradation — a
  bounded honest approximation, logged nowhere per-event (counted nowhere
  either; could add a counter if it ever matters).
- Remix is policy-light: average-down to mono, duplicate mono up,
  modulo-copy otherwise (logged once). A real downmix matrix (5.1→stereo
  weights) is future work; named here so it isn't mistaken for one.
- wasm capture processing grants are reported all-false at open even when
  the browser honors the constraints (truth arrives async after getUserMedia
  resolves; subsequent opens of the same device get real actuals). The
  honest floor, but a `granted`-update event would be better — wireframe
  question for later.
- The audiocapture test target compiles audioin's core sources directly
  (cross-module relative paths in build.c) to keep host providers out of
  hermetic tests — same pattern as audioin's own test, now spanning module
  boundaries. If a third module needs it, a shared mock-provider fixture is
  warranted.

## CLAUDE.md suggestions (recommendations only)

- None new.

## Suggestions

- Next per the order: the `audio` delta (engine binds Mel_AudioOut via the
  promoted pull bridge, taps, pull sources), then audioplayback, tts, stt.
- win-pilot first action when reachable: `git pull` + `nob build audioin
  audioout audiopolicy audiocapture` (audio too once the delta lands).
- Parked for wireframe review: drop-oldest overrun semantics (above), an
  async `granted`-update surface for web capture, `duck_ended` adoption in
  usage examples.
