# rng — specification

## Contracts

- **`Mel_Rng`** is `{ u64 (*next)(void*); void* state; }`. `next` returns a full
  64-bit word with all bits usable. Generators with narrower native output
  widen to 64 bits inside their thunk (pcg32 concatenates two 32-bit draws,
  high word first).
- All distributions are pure functions of the words drawn; they hold no state
  beyond the borrowed generator and allocate nothing.
- `Mel_Rng` borrows its generator by pointer. Lifetime of the generator must
  cover every use of the `Mel_Rng`.
- Determinism: a generator seeded identically yields an identical sequence on
  every platform and build. Only `<rng/entropy.h>` is non-deterministic.

## Quality decisions

- **f64 ∈ [0,1)** uses the top 53 bits × 2⁻⁵³; **f32 ∈ [0,1)** the top 24 bits ×
  2⁻²⁴. High bits are taken because xoshiro's low bits are weakest.
- **`below_u32`** is Lemire's multiply-high with the rare rejection branch —
  one multiply in the common case, no modulo, no bias.
- **`below_u64`** uses threshold rejection (`(-bound) % bound`) then one modulo;
  unbiased across the full 64-bit range.
- **`range_*`** are inclusive of both endpoints. Integer spans route through the
  unbiased `below_*`; degenerate `hi <= lo` returns `lo`.
- **`normal`** is Marsaglia polar (one deviate returned, its pair discarded to
  stay stateless). **`exponential`** is inverse-CDF on `1 - f64` so the log
  argument is never zero.
- **`shuffle`** is Fisher–Yates with an unbiased `below_u64` per step; it swaps
  byte-wise so any element size works with no scratch allocation.

## Generators

`splitmix64` (Steele–Lea–Flood), `pcg32` (O'Neill PCG XSH-RR 64/32), and
`xoshiro256**`/`++` with `jump`/`long_jump` (Blackman–Vigna). xoshiro state is
seeded through splitmix64, which guarantees the all-zero fixed point is never
the seed. The pcg32 reference vector (seed 42, stream 54) is pinned in
`test/test.pcg32.c` as a bit-exactness guard.

## Extension recipe (MEL-ENGINE-IX)

To add a generator `foo`:

1. `include/rng/foo.h` (+ `.inl`): a value struct `Mel_Foo`, an inline
   `mel_foo_next(Mel_Foo*) -> u64`, a constructor, and a thunk + `mel_foo_rng`
   adapter returning `Mel_Rng`.
2. Drop it. No edits to `rng.h`, `build.c` (the `src/*.c` glob is for
   non-inline code only), or any caller. Header-only generators need no source.
3. Add `test/test.foo.c` with a reference vector if the algorithm publishes one,
   else determinism + a moment check.

## Boundaries / not yet

- Entropy is implemented for posix (`getentropy`) and win32 (`BCryptGenRandom`).
  A `web`/`wasi` target that needs `mel_rng_entropy` must add `src/wasi/` (via
  `__wasi_random_get`); `mel_rng_entropy_u64` asserts on OS failure rather than
  silently degrading (MEL-ENGINE-VIII).
- No cryptographic generator. `entropy` is a CSPRNG seed source, but the
  generators here are non-cryptographic; do not use them for secrets.
