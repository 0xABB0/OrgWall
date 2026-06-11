# audio — device binding, taps & pull source specification

Scope: the engine's binding to output devices (`audioout`), output/voice
taps, and the generic pull source. The mixer core (engine, voices, sources,
faders, ring, mix thread) is specified by `design/audio-mixer-core.md` and
`readme.md`; this contract layers on top.

## Device binding

```
Mel_Audio_Opt.device : Mel_AudioOut
```

`mel_audio_create` requires `mel_audioout_init`: the engine's device plane is
an `audioplayback` pull-mode client, so every device open — default included —
routes through the `audioout` provider registry and virtual outputs are
reachable through the same single path (no registry-free side door,
MEL-ENGINE-IX). The compile-time backend ABI (`audio/backend.h`) becomes the
host `audioout` provider in implementation; the mixer is one `audioout`
client among many, not a toll gate.

- `MEL_AUDIOOUT_NULL` — follow the system default: the OS (or the host
  provider where the OS won't) migrates playback when the default changes.
  A named, documented semantic, never a silent default (MEL-CODE-007).
- A live handle — pinned to that device, any provider. A dead handle fails
  create loudly (NULL return, log).

`mel_audio_set_device(eng, device)` rebinds live: the provider stream closes,
reopens on the target; voices, faders, and the command queue survive; the
granted format renews. Returns `OK`, `WARNED | *` (rate resampled / channels
remixed / block adjusted — the existing warn bits), or `ERROR | NO_DEVICE`
(dead handle; previous binding stays). `mel_audio_device` returns the current
binding; `mel_audio_device_status` is the live binding state.

## Device loss & interruption

Pinned device vanishes: `Mel_Audio_Device_Event{ lost = true }` fires on
`mel_audio_device_events` and the engine holds — mixing pauses, voices
freeze, the clock stops; `mel_audio_device_status` reports
`ERROR | DEVICE_LOST` until the app rebinds (`set_device`). Never a silent
re-route (MEL-CODE-007). Follow mode instead migrates and fires
`{ default_changed }` (+ `{ format_changed }` when the new route
renegotiated).

OS interruption (phone call, Siri — the hardware is taken):
`{ interrupted = true }` fires and the engine holds exactly as for loss; when
the OS signals end-with-should-resume, the engine reopens and fires
`{ resumed = true }`. Resuming on the OS's signal is honoring OS policy, not
a silent default; an app that wants different behavior observes `audiopolicy`
events and rebinds itself. The event carries the affected `Mel_AudioOut`
handle.

## Taps

```
Mel_Audio_Tap* mel_audio_tap_open(eng, alloc, ring_frames);               // post-master mix
Mel_Audio_Tap* mel_audio_voice_tap_open(eng, voice, alloc, ring_frames);  // one voice, post-fader
u32 mel_audio_tap_read(tap, interleaved_dst, max_frames);
```

A tap is a wait-free `pcm` ring the mix thread writes beside the normal path
— the observer's window into the engine: visualizers (`spectrum`), mix
recording, streaming. Engine-format interleaved f32 (`mel_audio_caps`).
Multiple taps coexist; each costs exactly one ring write per block, paid only
while open (MEL-ENGINE-III). A slow tap reader drops oldest frames, counted
in `mel_audio_tap_dropped_frames` — measured, never silent. A voice tap dies
with its voice (reads drain, then return 0). Taps observe; they never mutate
the mix.

## Pull source

```
typedef u32 (*Mel_Audio_Pull_Fn)(void* user, f32* interleaved_dst, u32 frames);
Mel_Audio_Source* mel_audio_pull_source(const Mel_Alloc* a, Mel_Audio_Pull_Fn fn, void* user,
                                        u32 channels, u32 samplerate);
```

One general door for streamed audio into the mixer (MEL-ENGINE-IX): the mix
thread pulls; a short read is silence-padded for that block and the voice
stays live (a pull source has no natural end — stop it like any voice). `fn`
runs on the mix thread and must be wait-free (`mel_audiocapture_read` and
`mel_audioout_publish_read` qualify by contract). The declared format lowers
through the engine's per-voice remix/resample like any source. Zero
channels/samplerate are loud violations (MEL-CODE-007). Single-instance: one
producer, one voice.

Compositions this closes: mic monitoring/karaoke (capture → pull source),
virtual-cable playback (published output's read side → pull source),
visualizer (tap → `spectrum`), TTS into the mix (`tts` render →
`mel_audio_pcm_from_float`).

## Dependencies (delta)

- `audioout` — output identity: handles in opt/events.
- `audioplayback` — the device plane (pull-mode stream the mix thread
  serves).
- `pcm` — resampler contract (`Mel_Audio_Resampler` = `Mel_Pcm_Resampler`),
  tap rings.

## Test contract (delta)

Offline-engine tests: pull-source padding and lowering; tap content equals
rendered output, voice-tap post-fader correctness, drop accounting.
Mock-provider tests: set_device voice survival, loss-hold-recover,
interruption hold/auto-resume, event payloads, create-without-audioout-init
asserting loudly. No hardware.
