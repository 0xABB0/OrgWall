# 2026-05-31 — curve module

## Work done

Extracted the Bézier timing-curve code from `math/curve.{h,fwd.h,inl}` +
`src/curve.c` into a dedicated `modules/curve`, the third same-day extraction
after `rng` and `easing`. Curve is the open, data-driven counterpart to the
fixed `easing` catalogue: where `easing` enumerates closed-form `mel_ease_*`
shapes, `curve` evaluates whatever cubic Bézier two control points describe — the
CSS `cubic-bezier(…)` path an editor or designer hands it.

- **Moved** (`git mv`, four files): the `Mel_Bezier` struct + `MEL_CURVE_*` /
  `MEL_BEZIER_SEGMENTS` constants (`curve.h`), the forward-declaration
  (`curve.fwd.h`), the `mel_curve_eval` table lookup (`curve.inl`), and the
  forward-difference baker `mel_bezier_init` (`src/curve.c`). Bodies unchanged;
  the lone edit is `curve.c`'s include, `<math/curve.h>` → `<curve/curve.h>`.
- **Namespace** `<curve/curve.h>`, prefixes `mel_curve_` / `mel_bezier_`. Unlike
  the header-only `easing`, `curve` carries a compiled source, so it mirrors the
  `rng` shape: library + `src/*.c`, depending only on `core` (`assert` arrives
  via `<core/types.h>`'s `<assert.h>`; `f32`/`i32`/`u32` are core types). No edge
  to `easing`, `scalar`, or `mpfr`.
- `math/build.c` needs **no edit**: it globs `src/*.c`, which now resolves to
  `real.c` alone — the AR step archives only `real.o`. No caller anywhere in
  `modules`/`apps`/`tools` referenced curve (verified), so the move breaks
  nothing.
- **readme.md** / **spec.md** in the `rng`/`easing` register, cross-linking
  `easing` as the sibling; **test/test.curve.c**: `linear` identity, `stepped`
  hold-zero, the diagonal Bézier (collinear controls) reproducing the identity,
  endpoint pinning incl. out-of-range clamp, and monotone-bounded over an
  ease-in-out.

### Verification

- `./nob build curve` compiles `curve.o` and archives `libcurve.a`;
  `./nob build math` re-archives `libmath.a` from `real.o` alone — clean without
  the curve source.
- `test/test.curve.c` syntax-checks against the harness (exit 0).
- Numeric check (standalone, linking the real `mel_bezier_init`, since
  `./nob test` still has no runner): linear identity, stepped 0, diagonal Bézier
  ≈ identity to 1e-3, endpoints pinned, monotone+bounded, and
  `ease-in-out(0.25) = 0.1329` (correctly slow-start) — all pass.
- All new C reformatted with the repo `clang-format` (MEL-CODE-004).

## Kludges (MEL-ENGINE-VIII — confess all)

Both are **pre-existing**, relocated byte-for-byte from `math`, not introduced —
flagged in `spec.md` and surfaced to Gabbo at extraction time. A move is not a
mandate to redesign; reshaping either needs his approval (MEL-CODE-001 carve-out).

- **`MEL_CURVE_{LINEAR,STEPPED,BEZIER}` model a closed set with constants** —
  enum-adjacent (MEL-CODE-001). The idiomatic reshape is to dispatch on an open
  sampler (a function pointer, the shape `rng`/`allocator` use), collapsing
  `LINEAR`/`STEPPED` into degenerate samplers. Deferred.
- **`f32 samples[MEL_BEZIER_SEGMENTS]` is a fixed-size array** (MEL-CODE-002). It
  is a baked-resolution constant rather than a `MEL_MAX_*` capacity cap, so the
  failure mode is "can't ask for finer sampling," not "overflow" — milder, but a
  caller-chosen sample buffer sized at bake would honor the rule. Deferred.

## CLAUDE.md suggestions (recommendations only — not applied)

- Same drift the `rng`/`easing` writeups flagged: `CLAUDE.md` points at
  `tools/build/` and claims modules carry no `build.c`; the live system is
  `modules/build/` and every module has one. Three extractions now corroborate it.

## Suggestions

- **`math` is now lean** — `real.c` (mpfr-backed) + the header-only `scalar`,
  `vector`, `mat`, `geo` trees. The earlier-suggested `scalar` carve-out (so
  `easing` depends on `scalar` + `core` instead of all of `math`/`mpfr`) is the
  natural next move and would leave `math` as a pure umbrella over its geometry
  headers.
- **`tween` module** atop `easing` + `curve` + `time`: a played handle that
  samples either an `mel_ease_*` function or a baked `Mel_Bezier` over a duration
  — the consumer that would finally exercise both interpolation modules and the
  `MEL_EASING_LIST` registry.
- **Unify the two `step`/`stepped` holds**: `easing`'s `mel_ease_step` and
  `curve`'s `MEL_CURVE_STEPPED` are the same degenerate curve expressed twice. If
  a `tween` layer sits above both, it should name the hold once.
