# audiocapture

Audio input as a pull stream, and nothing else: open a `Mel_AudioIn`, read
interleaved f32 frames at your own cadence, close. Device identity,
enumeration, consent, and gain live in `audioin`; this module never
enumerates, never prompts.

Providers push native frames through `audioin`'s sink plane; this module
rings them (`pcm` SPSC frame ring) and converts once (resample with
one-sample streaming history, channel remix) — one conversion
implementation for every provider, virtual ones included. Voice processing
and exclusive access are negotiated through the provider open and reported
back honestly in `granted`, with every refusal named in the open status.
`read_ex` carries per-batch capture timestamps (OS stamps where the
provider grants them, ring-arrival time otherwise). See `spec.md`.

Dependencies: `core`, `allocator`, `audioin`, `pcm`, `time`, `log`.
