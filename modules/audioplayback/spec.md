# audioplayback — specification

Audio output as a raw stream, and nothing else: open a `Mel_AudioOut`, move
interleaved f32 frames to it, close. The thin output door — the exact twin of
`audiocapture` on the other side of the identity seam
(`audiocapture : audioin :: audioplayback : audioout`). For consumers that
bring their own engine — a DAW graph, a custom synth, an emulator core — the
mixer (`audiomixer`) is just one sibling client of `audioout`, not a toll gate
(MEL-ENGINE-IV).

## Two modes, one open

```
Mel_AudioPlayback_Opt {
    u32 sample_rate;              // stream format, exact
    u32 channels;
    u32 ring_capacity_frames;     // write mode: buffer depth
    Mel_AudioPlayback_Pull_Fn pull;   // pull mode: engine fills directly
    void* user;
    bool exclusive;               // raw/exclusive device access
}
Mel_AudioPlayback_Open_Result { Mel_AudioPlayback* playback; Mel_AudioPlayback_Status status; }
```

Exactly one mode must be chosen — `ring_capacity_frames > 0` XOR
`pull != NULL`; both or neither is a loud contract violation (MEL-CODE-007).

- **Write mode** (simple path): the app writes frames at its own cadence into
  a `pcm` SPSC ring; the provider's clock drains it. An empty ring on the
  provider's pull pads silence and counts every padded frame
  (`WARNED | WARN_UNDERRUN`, `mel_audioplayback_underrun_frames`) — starvation
  is measured, never silent (MEL-ENGINE-VIII).
- **Pull mode** (pro path, same door): the provider's pull lands directly in
  the caller's `pull(user, dst, frames)` — zero copies between the engine and
  the device. `pull` runs on the provider's realtime thread: wait-free code
  only; a short fill is the caller's own silence to own.

`sample_rate`/`channels` are mandatory and delivered exactly: device-native
mismatches convert inside (OS converter or `pcm`), named at open with
`WARNED | WARN_CONVERTED`.

## Open

- Requires `mel_audioout_init` and a live handle (`ERROR | NO_DEVICE` when
  dead). Any provider's device — virtual sinks included — through the one
  provider-routed path (MEL-ENGINE-IX).
- `exclusive` requests raw/exclusive access; refusal is
  `WARNED | WARN_EXCLUSIVE_DROPPED` and the truth lives in
  `mel_audioplayback_granted`. Device held exclusively elsewhere:
  `ERROR | BUSY`. Unsatisfiable format: `ERROR | UNSUPPORTED`.

## Write, latency, status, loss

- `write` returns frames accepted (`0..frames`; ring full rejects loudly via
  the return). `writable` is current free depth. Both wait-free
  (MEL-ENGINE-VI).
- `mel_audioplayback_latency_frames` reports the output path latency the
  provider grants (device buffer + ring fill) — the number a DAW aligns
  recordings against; `granted.os_timestamps` says whether it is the OS's
  figure or the module's bound.
- Device vanishes: sticky `ERROR | LOST`; writes are rejected (return 0) with
  the status saying why; pull mode stops being called. Recovery is a new open
  against a new handle; hotplug arrives via `audioout`.

## Ownership & memory

`open` allocates everything from the caller's allocator; `close` returns it
(MEL-CODE-003). No global state; concurrent streams (same or different
devices) are independent — the provider arbitrates device sharing.

## Concurrency

Provider's thread is the consumer (write mode) or the caller of `pull` (pull
mode); the app side is one producer/control thread. `write`/`writable`/
`status`/`underrun_frames` belong to the producer thread; `open`/`close` to
the control thread.

## Composition

A DAW graph or synth renders into `write` (or serves `pull`); `audiomixer` itself
is implementable over this door (and that is the implementation intent: the
engine's device plane becomes an `audioplayback` pull-mode client).
`mel_audioout_publish_read → mel_audioplayback_write` is a software patch
cable between apps.

## Platform notes

Format conversion, exclusive, and latency reporting lower per host provider:
macOS/iOS (AudioUnit IO / hog mode, kAudioDevicePropertyLatency), win32
(WASAPI shared/exclusive, IAudioClient latency; ASIO arrives as a provider,
not a special case), linux (PipeWire stream latency), android (AAudio
performance modes), wasm (AudioWorklet; no exclusive). Every refusal named.

## Dependencies

- `core` — types, asserts.
- `allocator` — every byte from the caller's allocator.
- `audioout` — identity and the provider pull plane.
- `pcm` — the write-mode ring; conversion.
- `log` — open/loss diagnostics.

## Test contract

Against a mock `audioout` provider: mode XOR validation, open gating
(dead/unsupported/busy), exact-format delivery + `WARN_CONVERTED`,
exclusive lowering honesty, write-mode underrun padding + accounting,
pull-mode direct delivery, latency reporting, sticky-LOST rejection,
allocator round-trip. No hardware.
