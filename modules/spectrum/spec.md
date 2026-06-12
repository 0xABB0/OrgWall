# spectrum — specification

Frequency-domain analysis over PCM windows: real-input FFT, window functions,
magnitude/phase extraction, bin↔Hz mapping. Pure DSP — `f32` samples in,
spectra out; no platform code, no threads. The analysis gap beside
`pitchdetect` (time-domain): a visualizer is `tap/capture read → window →
analyze → draw`.

## Analyzer

`Mel_Spectrum` is opaque; `create(alloc, window_frames)` sizes all scratch and
twiddle tables once from the caller's allocator (MEL-CODE-003);
`mel_spectrum_analyze` allocates nothing and is safe to call per frame at
visualizer rate (MEL-ENGINE-VI). `window_frames` must be a power of two —
anything else asserts loudly (MEL-CODE-007: no silent rounding).

- `analyze` — windowed samples in, `bins` magnitudes out (`window/2 + 1`).
- `analyze_complex` — re/im planes out, for consumers that need phase
  (reassignment, vocoder experiments).
- `mel_spectrum_bins(s)` — bin count; `mel_spectrum_bin_hz(bin, window,
  samplerate)` — pure mapping helper.

## Windows

Window functions are plain named functions over caller buffers (`hann`,
`hamming`, `blackman`), `dst = src * w[i]` — an open set: a new window is a
new function, never a new enum case (MEL-CODE-001). Callers that skip
windowing pass raw samples and own the spectral leakage; nothing is applied
implicitly (MEL-CODE-007).

## Failure

Debug asserts on NULL buffers, non-power-of-two windows, and aliasing
`analyze` in/out. No runtime error codes — misuse is a bug (MEL-ENGINE-VIII).

## Dependencies

- `core` — types, asserts.
- `allocator` — analyzer scratch via the caller's `Mel_Alloc`.

## Consumers

Visualizers over `mel_mixer_tap_read` / `mel_audiocapture_read`; `pitchdetect`
stays independent (YIN needs no FFT). This module knows none of them.
