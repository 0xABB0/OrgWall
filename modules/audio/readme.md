# audio

A SoLoud-shaped PCM mixing and playback engine: the engine, the voice-handle model, the source
producer, raw-PCM playback, per-voice gain/pan/resample, and faders — over a native backend per
platform plus an offline (no-device) engine mode.

The device thread does one bounded `memcpy` out of a module-owned lock-free SPSC float ring
(`Mel_Audio_Ring`); the `thread`-spawned mix thread drains the API→mix command queue, mixes every
live voice into planar scratch, and refills the ring. The backend is chosen at compile time by
`build.c` source gating (one native backend per platform supplies the four §6 fixed-name ABI
symbols `mel_audio_backend_{open,start,stop,close}`); there is no runtime backend object.

The control plane rides `future` (voice-end, device-started), `event` (device hotplug/format),
and `channel` (API→mix transport) over a `Mel_Executor`, the `clipboard` substrate shape. Voices are
handles over `collection.slotmap`; the slotmap generation guards stale handles. All memory flows
through a caller-supplied `Mel_Alloc`.

Backends (one compiles per platform): CoreAudio (macOS/iOS), WASAPI (win32), ALSA (linux),
AAudio (android), AudioWorklet/Web Audio (wasm).

Spec: `design/audio-mixer-core.md`. Dependencies: `core`, `allocator`, `collection`, `math`,
`thread`, `time`, `reactor`, `executor`, `future`, `event`, `channel`, `log`, `debug`, `string`.
