# audiocapture — specification

Audio input as a pull stream, and nothing else: open a `Mel_AudioIn`, read
interleaved f32 frames at your own cadence, close. Device identity,
enumeration, consent, and gain live in `audioin`; this module never
enumerates, never prompts. Deliberately thin — that is the cost of the clean
seam: a tuner links capture and identity, never the mixer; a settings panel
links identity, never this.

## Stream contract

```
Mel_AudioCapture_Opt {
    u32 sample_rate;                 // delivered rate, exact
    u32 channels;                    // delivered interleaved channel count
    u32 ring_capacity_frames;        // pull buffer depth
    bool exclusive;                  // raw/exclusive device access
    Mel_AudioCapture_Processing processing;   // { echo_cancellation, noise_suppression, auto_gain }
}
Mel_AudioCapture_Open_Result { Mel_AudioCapture* capture; Mel_AudioCapture_Status status; }
```

`sample_rate`, `channels`, `ring_capacity_frames` are mandatory; zero is a
loud contract violation (MEL-CODE-007). The module delivers exactly the
requested format: the provider pushes native frames (see `audioin`'s sink
plane), this module rings them (`pcm` SPSC frame ring) and converts once (OS
converter where offered, `pcm` resample/remix otherwise) — one conversion
implementation for every provider, virtual ones included. Conversion is named
at open: `WARNED | WARN_CONVERTED` (MEL-ENGINE-VIII).

## Voice processing & exclusive

`processing` requests the OS voice-processing stack (echo cancellation, noise
suppression, automatic gain) — the voice-chat trio. `exclusive` requests
raw/exclusive device access (pro audio). Both lower honestly: each granted
flag is reported back by `mel_audiocapture_granted`, each refusal is named
(`WARNED | WARN_PROCESSING_DROPPED`, `WARNED | WARN_EXCLUSIVE_DROPPED`) —
never silently absent, never silently emulated. Requesting both processing
and exclusive is itself lowered per platform (most OSes give one or the
other).

## Open

- Requires `mel_audioin_init` and a live handle: dead handle →
  `ERROR | NO_DEVICE`.
- Requires granted consent: else `ERROR | DENIED`. Open never triggers the
  OS prompt — that is `mel_audioin_authorize`'s job alone.
- Unsatisfiable format (no native path, no conversion path):
  `ERROR | UNSUPPORTED`. Device held exclusively elsewhere: `ERROR | BUSY`.

## Read, timestamps, status, loss

- `read` returns frames delivered (`0..max_frames`), interleaved, exact
  format. `read_ex` additionally yields the capture timestamp of the first
  returned frame (`timestamp_ns`, OS monotonic clock) — the AV-sync/lip-sync
  contract; the OS capture stamp where offered, ring-arrival time otherwise,
  which one it is reported in `granted.os_timestamps`.
- `available` is current ring depth; both read paths and `available` are
  wait-free (MEL-ENGINE-VI).
- `status` is live: device vanished → sticky `ERROR | LOST`; buffered frames
  keep draining, then reads return 0 with the status saying why — never a
  silent zero-filled stretch. Recovery is a new open; hotplug arrives via
  `audioin`.
- Overrun (slow reader) drops oldest frames: sticky-until-read
  `WARNED | WARN_OVERRUN`, every dropped frame counted in
  `mel_audiocapture_dropped_frames` — loss is measured, never silent.

## Ownership & memory

`open` allocates everything (ring included) from the caller's allocator;
`close` returns it all (MEL-CODE-003). No global state; concurrent captures
on any devices are independent.

## Concurrency

Producer is the provider's thread (owned by the provider, never spawned here
— MEL-ENGINE-III); consumer is one app thread. `read`/`read_ex`/`available`/
`status`/`dropped_frames` belong to the consumer thread; `open`/`close` to
the control thread.

## Composition

`mel_audiocapture_read` matches `audio`'s `Mel_Audio_Pull_Fn` — mic→mixer is
one `mel_audio_pull_source` call, no dependency either way (MEL-ENGINE-IX).
`pitchdetect`/`spectrum` consume the read buffer directly. `stt` fed sessions
pump from here. Loopback devices (`audioin` kind) make this the system-audio
reader with zero new API.

## Platform notes

Conversion/processing/exclusive lowering per host provider: macOS/iOS
(AudioQueue / AVAudioEngine voice-processing IO, hog mode), win32 (WASAPI
capture + APO effects, exclusive mode, loopback endpoints), linux (PipeWire
streams + echo-cancel module), android (AAudio input presets,
VOICE_COMMUNICATION for AEC, performance mode for exclusive), wasm
(getUserMedia constraints: echoCancellation/noiseSuppression/
autoGainControl; no exclusive). Each refusal named in the open status.

## Dependencies

- `core` — types, asserts.
- `allocator` — every byte from the caller's allocator.
- `audioin` — identity, consent, the provider push plane.
- `pcm` — ring, resample/remix.
- `time` — monotonic timestamps for the ring-arrival fallback.
- `log` — open/loss diagnostics.

## Test contract

Against a mock `audioin` provider: open gating (denied/dead/unsupported/
busy), exact-format delivery + `WARN_CONVERTED`, processing/exclusive
lowering honesty in `granted` + warn bits, frame-granular ring, overrun
accounting, sticky-LOST drain, timestamp monotonicity and source reporting,
allocator round-trip. No hardware.
