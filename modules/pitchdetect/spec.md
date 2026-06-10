# pitchdetect

Fundamental-frequency estimation over PCM windows. Pure DSP: no platform code, no
audio-device knowledge, no theory types — `f32` samples in, `f64` Hz out. The tuner
pipeline converts the estimate with `mel_freq()` and quantizes with
`mel_tuning_find_index`.

## Algorithm

YIN (de Cheveigné & Kawahara):
1. Difference function over the lag range derived from `[min_hz, max_hz]`.
2. Cumulative mean normalized difference (CMNDF).
3. First lag under `threshold` (absolute-threshold step), local-minimum refined.
4. Parabolic interpolation around the chosen lag for sub-sample precision.
5. Clarity = `1 - cmndf(lag)`; below-threshold windows report `voiced = false`.

Lag range is bounded by the config, so cost is `window × lags`, not `window²`.

## API shape

- `Mel_PitchDetector` is a container: config + scratch buffer sized at construction
  from the caller's allocator. `mel_pitch_detect` performs zero allocation.
- All config explicit (sample rate, window, min/max Hz, threshold) — no defaults
  (MEL-CODE-007). Contract violations assert (window too small for `min_hz`,
  inverted range, zero rate).
- `Mel_Pitch_Estimate { f64 frequency_hz; f32 clarity; bool voiced; }`.
- An unvoiced estimate carries `frequency_hz = 0` and the clarity that rejected it.

## Tests

Synthesized input at known frequencies: pure sine (440, 261.626), sawtooth with
strong harmonics (octave-error trap), low-amplitude sine, white noise → unvoiced,
silence → unvoiced, frequency sweep across two windows. Accuracy bar: within 1 cent
for pure tones at 48 kHz / 2048-sample windows.

## Dependencies

core, allocator. Nothing else.
