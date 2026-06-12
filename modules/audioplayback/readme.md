# audioplayback

Audio output as a raw stream: open a `Mel_AudioOut`, move interleaved f32 frames to it, close.
The exact output twin of `audiocapture` — write mode buffers through a `pcm` SPSC ring drained by
the provider's clock (underruns padded and counted), pull mode lands the provider's pull directly
in the caller's callback (zero copies). Device-native format mismatches convert inside (streaming
linear resample + remix), named at open with `WARN_CONVERTED`; exclusive access and latency are
negotiated through the `audioout` provider plane and reported honestly from read-back.

Spec: `spec.md`. Dependencies: `core`, `allocator`, `audioout`, `pcm`, `log`.
