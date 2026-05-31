# easing — specification

## Contracts

- Each `mel_ease_<name>` is a pure `f32 -> f32` with no state and no allocation.
  The intended domain is `t ∈ [0,1]`; inputs outside it are not clamped — the
  caller normalizes time (see `mel_normalize_time` / `mel_saturatef` in
  `<math/scalar.h>`).
- Endpoint convention: `mel_ease_<name>(0) == 0` and `mel_ease_<name>(1) == 1`
  for every curve except `step`, which is `0` across `[0,1)` and whose `t == 1`
  value is unspecified by intent (it is a hold, not a ramp). The `expo` and
  `elastic` families special-case the exact endpoints `0` and `1` so the
  `pow`/`sin` machinery does not perturb them.
- `in_out_*` are piecewise about `t == 0.5`; the two halves meet at `0.5`.
- The `back` and `elastic` families return values outside `[0,1]` in the
  interior — overshoot and recoil are the point. Callers that interpolate a
  bounded quantity must tolerate this or pick a monotone family.

## Constants

`back` uses `c1 = 1.70158` (≈ 10% overshoot) and its `in_out` derivative
`c2 = c1 * 1.525`. `elastic` uses period `TAU/3` (in/out) and `TAU/4.5`
(in_out). `bounce` uses `n1 = 7.5625`, `d1 = 2.75`. These are the canonical
easings.net / Penner figures and are pinned by the bodies, not configurable.

## Dependency

Compile-time only: `<easing/easing.h>` includes `<math/scalar.h>` for the
transcendentals and angle constants. `scalar` is header-only over compiler
builtins, so an easing consumer pulls no `math` (or `mpfr`) object — static
archive resolution links nothing it does not reference (MEL-ENGINE-III). The
`build.c` `mel_depends("math")` exists to propagate the include path; there is
no link edge to satisfy.

## Registry (MEL-ENGINE-IX)

`MEL_EASING_LIST(X)` is the single source of truth for the curve set; expand it
to build any name-indexed structure. `MEL_EASING_COUNT` (32) is the count.
Adding a curve is a header edit: declare + define `mel_ease_<name>`, add one
`X("<name>", mel_ease_<name>)` row, bump the count. No `build.c`, no caller
change — header-only, so there is no source glob to touch.

## Boundaries / not yet

- No parametric easings (configurable overshoot, custom cubic-bezier control
  points). The set is the fixed Penner family; a bezier-driven curve would be a
  distinct constructor, not a member of this table.
- `MEL_EASING_COUNT` is maintained by hand alongside `MEL_EASING_LIST`. It is a
  pre-existing manual count carried over verbatim from `math`; deriving it from
  the X-macro is a worthwhile follow-up but out of scope for the extraction.
