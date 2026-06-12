# audiomixer

A SoLoud-shaped PCM mixing and playback engine: the engine, the voice-handle model, the source
producer, raw-PCM playback, the generic pull source, per-voice gain/pan/resample, faders, and
output/voice taps — over the `audioout`/`audioplayback` device plane plus an offline (no-device)
engine mode.

The device plane is an `audioplayback` pull-mode client: the provider's clock pulls interleaved
frames out of a module-owned lock-free SPSC float ring (`Mel_Mixer_Ring`); the `thread`-spawned mix
thread drains the API→mix command queue, mixes every live voice into planar scratch, and refills
the ring. Every device open — default included — routes through the `audioout` provider registry,
so virtual outputs ride the same path. `MEL_AUDIOOUT_NULL` follows the system default and migrates
on `default_changed`; a pinned device that vanishes holds the engine (`ERROR | DEVICE_LOST`) until
rebound via `mel_mixer_set_device`. OS interruptions hold and auto-resume through `audiopolicy`
events when the policy session is initialized.

The control plane rides `future` (voice-end), `event` (device loss/migration/interruption),
and `channel` (API→mix transport) over a `Mel_Executor`. Voices are handles over
`collection.slotmap`; the slotmap generation guards stale handles. Taps are wait-free `pcm` rings
the mix thread writes beside the normal path; drops are counted, never silent. All memory flows
through a caller-supplied `Mel_Alloc`.

Spec: `design/audio-mixer-core.md` (mixer core) + `spec.md` (device binding, taps, pull source).
Dependencies: `core`, `allocator`, `collection`, `math`, `thread`, `time`, `executor`, `future`,
`event`, `channel`, `log`, `debug`, `string`, `pcm`, `audioout`, `audioplayback`, `audiopolicy`.
