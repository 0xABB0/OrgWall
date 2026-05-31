# rng

Pseudo-random number generation: a small set of well-characterized bit-source
generators behind one type-erased interface, plus the distributions every
caller re-derives badly. Extracted from the single biased xorshift that lived in
`math`. Namespace `<rng/...>`, symbol prefix `mel_rng_` (generators carry their
own prefix, e.g. `mel_pcg32_`).

## Why

Randomness is needed everywhere — procedural content, sampling, fuzzing,
load-shuffling, id minting, Monte-Carlo. The old `math/rng.h` shipped one
xorshift64 with three latent defects: modulo bias in `bounded`, a low-entropy
`f32`, and a degenerate seed clamp. Every call site that wanted a different
quality/speed trade-off, an unbiased range, or a normal deviate re-rolled it.
This module owns that vocabulary once, correctly, and leaves room to add
generators without touching callers (MEL-ENGINE-IX).

## Model

The boundary type is `Mel_Rng` — a `next()` function pointer over an opaque
state, the same callback shape `allocator` uses. It yields a canonical 64-bit
word; every distribution consumes that word. Concrete generators are plain
value structs with an inline `*_next` (zero-overhead hot path) and an adapter
`*_rng(&g)` that binds them into a `Mel_Rng` (the uniform path). The simple path
and the fast path are the same path, walked further (MEL-ENGINE-II).

`Mel_Rng` borrows the generator; the generator must outlive it.

## Generators

- **`splitmix64`** — 64-bit state, the canonical seeder (and a serviceable
  generator). Seeds the 256-bit generators from a single `u64`.
- **`pcg32`** — PCG XSH-RR 64/32. The recommended default: small state,
  excellent statistics, selectable stream. Matches O'Neill's reference vector.
- **`xoshiro256**` / `xoshiro256++`** — 256-bit, very fast, top-tier quality;
  `**` for floating point, `++` for raw bits. `jump`/`long_jump` mint
  non-overlapping streams for parallelism.

Adding one is a self-contained header drop (`<rng/yourgen.h>` exposing
`*_next` + `*_rng`); nothing else changes.

## Distributions

Over `Mel_Rng`: `next`/`u32`/`bool`, `f32`/`f64` in `[0,1)`, unbiased
`below_u32`/`below_u64` (Lemire + rejection) and `index`, inclusive
`range_i32`/`range_i64`/`range_f32`/`range_f64`, `chance`, `normal`/
`normal_about` (Marsaglia polar), `exponential`, `fill`, and in-place
Fisher–Yates `shuffle`. No allocation; the module depends only on `core`.

## Entropy

`<rng/entropy.h>` — `mel_rng_entropy` fills bytes from the OS CSPRNG
(`getentropy` on posix/apple, `BCryptGenRandom` on win32), with
`mel_pcg32_seeded` / `mel_xoshiro256_seeded` for non-deterministic seeding.
