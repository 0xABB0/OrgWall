# 2026-05-31 — easing module

## Work done

Extracted the Penner easing family from `math/easing.{h,inl}` into a dedicated
`modules/easing`, mirroring the same-day `rng` extraction. The split is motivated
by vocabulary, not arithmetic: easing curves are pure `f32 -> f32` shapes over
normalized time — animation's lingua franca — not scalar primitives, and had no
business sharing a roof with `min`/`clamp`/`lerp`.

- **Moved verbatim** (`git mv`): the 32 curves — `linear`; `in`/`out`/`in_out`
  across `quad`, `cubic`, `quart`, `quint`, `sine`, `circ`, `expo`, `elastic`,
  `back`, `bounce`; and the degenerate `step`. Bodies unchanged; the lone edit is
  the include, `"scalar.h"` → `<math/scalar.h>`, now that the header lives a
  module away.
- **Namespace** `<easing/easing.h>`, prefix `mel_ease_`. Header-only — the curves
  are a few FLOPs and inline straight into the caller (MEL-ENGINE-II).
- **Registry preserved**: `Mel_Easing_Func`, `Mel_Easing_Entry`, the
  `MEL_EASING_LIST(X)` X-macro, and `MEL_EASING_COUNT`, for data-driven name→func
  selection.
- **build.c** declares a header-only library depending on `math`. The emit step
  archives a library only when it has objects, so `easing` produces no `.a`; it
  contributes its public include path alone.
- Deleted nothing from `math/build.c` — easing was header-only there, never a
  compiled source, so its removal needs no build edit. No caller anywhere in
  `modules`/`apps`/`tools` included `math/easing.h` (verified), so the move breaks
  nothing.
- **readme.md** / **spec.md** in the `rng` register; **test/test.easing.c** with
  the contracts: registry cardinality, pinned endpoints (0→0, 1→1, `step`
  excepted), `linear` identity, `out(t) == 1 − in(1−t)` reflection, monotone
  families bounded in [0,1], `in_out` meeting at the midpoint, `step` holding 0.

### Addendum — `smooth` / `smoother` (Gabbo-approved)

Added the two graphics-heritage sigmoids that the Penner set omits, bringing the
table to **34**: `mel_ease_in_out_smooth` (Hermite `t²(3−2t)`, delegating to
`mel_smoothstepf(0,1,t)` per Gabbo's call, so the polynomial is stated once) and
`mel_ease_in_out_smoother` (Perlin `6t⁵−15t⁴+10t³`, no scalar equivalent, inline
over `mel_saturatef`). Both are inherently `in_out` — a single sigmoid each, no
in/out halves. Tests extended: midpoint 0.5, monotone bound, and an explicit
`smoothstep_family_clamps`. Verified numerically against hand-computed values
(`smooth(0.25)=0.15625`, both midpoints 0.5, both endpoints pinned, clamps hold)
— all pass. The addition needs only `<math/scalar.h>`, so the offered `curve`
extraction was **not required**; it remains the standing future split.

### Verification

- `./nob build easing` and `./nob build math` both succeed; `easing` emits no
  archive (expected, header-only) and pulls `math` for `<math/scalar.h>`.
- `test/test.easing.c` syntax-checks clean against the real harness and the four
  module include paths (`-std=c23 -fsyntax-only`, exit 0).
- All new C reformatted with the repo `clang-format` (MEL-CODE-004).

`./nob test` still does not synthesize a runner from `modules/*/test/*.c` (noted
in the rng writeup), so the test is forward-looking — it will execute once test
synthesis is restored.

## Kludges (MEL-ENGINE-VIII — confess all)

- **`MEL_EASING_COUNT` is a hand-maintained count** beside `MEL_EASING_LIST` (now
  34 — bumped by hand when `smooth`/`smoother` landed, exactly the desync risk).
  The test guards it (`registry_count_matches_list`), but the right fix is to
  derive the count from the list rather than restate it. Pre-existing debt,
  relocated, not introduced — flagged in `spec.md` as out-of-scope follow-up.
- **The `smooth` family clamps; the Penner curves do not.** `in_out_smooth` and
  `in_out_smoother` saturate their input to `[0,1]` (GLSL `smoothstep` heritage),
  whereas every other curve extrapolates past the endpoints. A divergence in
  out-of-domain behavior within one table — intentional, but a sharp edge. It sits
  inside the already-stated "caller normalizes time" envelope and is documented in
  `spec.md`; the `smoothstep_family_clamps` test pins it.
- **`easing` depends on the whole `math` module** to obtain one header,
  `<math/scalar.h>`. It is an include-only edge: `scalar` is header-only over
  compiler builtins, and static-archive linking is lazy, so a consumer pulls no
  `math`/`mpfr` object it does not reference — zero link cost in practice
  (MEL-ENGINE-III). Still, at module granularity the dependency nominally lists
  `libmath.a` on the link line. A `scalar`-only (or `mathcore`) header module
  would make the edge exact; see suggestions.
- **The `git mv` is no longer byte-identical** to the `math` originals: beyond the
  include line, `clang-format` re-collapsed the manually column-aligned
  `MEL_EASING_LIST` and the test arrays. Semantically identical; blame history
  still follows the rename.

## CLAUDE.md suggestions (recommendations only — not applied)

- Same drift the rng writeup flagged: the repo `CLAUDE.md` points at `tools/build/`
  and claims modules carry no `build.c`; the live system is `modules/build/` and
  every module has a `build.c`. Easing is one more datapoint that the prose misleads
  a new-module author.
- Document that a header-only library is a first-class shape (declare the target,
  `mel_includes` + `mel_depends`, no `mel_sources`) — `easing` is the first such
  module, and the behavior (no archive emitted) is non-obvious without reading
  `emit.c:241`.

## Suggestions

- **Carve a `scalar` (header-only) module** out of `math` so transcendental-only
  consumers — `easing`, and any future tween/curve code — depend on `scalar` + `core`
  instead of dragging the `math`/`mpfr` link closure. It would make `easing`'s
  dependency exact and unblock the same minimalism `rng` enjoys (core-only).
- **`tween` module** atop `easing` + `time`: a played handle that samples a curve
  over a duration, with `Mel_Easing_Func` selection — the natural next layer, and a
  consumer that would finally exercise the registry.
- **Extract `curve` next** the same way: it is the remaining non-scalar resident of
  `math` and a sibling to easing.
- Restore `modules/*/test/*.c` synthesis so `easing` (and `rng`, `barcode`) run
  under `./nob test`.
