# spectrum

Frequency-domain analysis over PCM windows: real-input FFT, window functions,
magnitude/phase extraction, bin↔Hz mapping. Pure DSP — `f32` samples in,
spectra out; no platform code, no threads. The analysis gap beside
`pitchdetect` (time-domain).

Magnitudes are unnormalized DFT values (a full-scale sinusoid at bin `k`
reads `window/2` at that bin); callers scale to taste. Window functions use
the symmetric textbook definitions. See `spec.md` for the full contract.

Dependencies: `core`, `allocator`.
