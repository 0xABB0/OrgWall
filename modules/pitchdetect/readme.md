# pitchdetect

Fundamental-frequency estimation over PCM windows (YIN: cumulative mean normalized
difference, absolute threshold, parabolic interpolation). Pure DSP — `f32` samples in,
`f64` Hz + clarity out; no platform code, no theory types.

`Mel_PitchDetector` sizes its scratch once at construction from the caller's
allocator; `mel_pitch_detect` allocates nothing. All configuration is explicit
(sample rate, window size, min/max Hz, voicing threshold). The lag search is bounded
by the configured frequency range, so cost is `window × lags`.

Unvoiced windows (silence, noise, below-threshold periodicity) report
`voiced = false` and `frequency_hz = 0` — never a guess.

Dependencies: core, allocator.
