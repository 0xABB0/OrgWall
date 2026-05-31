# curve

Author-defined timing curves: a cubic Bézier baked to a sample table at init,
read back by binary search, plus `linear` and `stepped` modes. The open,
data-driven counterpart to [[easing]]'s fixed Penner catalogue — where easing is
a closed set of named closed-form shapes, a curve is whatever two control points
the author (or a CSS `cubic-bezier(…)`, or a keyframe editor) hands it. Extracted
from `math`, where it was an animation primitive masquerading as scalar
arithmetic. Namespace `<curve/...>`, symbol prefixes `mel_curve_` / `mel_bezier_`.

## Why

A timeline needs interpolation the engine cannot enumerate in advance: the
designer drags a Bézier handle and expects that exact ease. The fixed easing
family cannot express it, so `curve` owns the data-driven path — bake once,
sample cheaply, no allocation. Splitting it from `math` lets the animation and
gui layers pull the timing-curve vocabulary without the scalar/matrix bulk
behind it, and lets the two interpolation modules (`easing`, `curve`) evolve
independently (MEL-ENGINE-IX).

## Model

`Mel_Bezier` is a value struct holding `MEL_BEZIER_SEGMENTS` baked samples.
`mel_bezier_init(&bez, cx1, cy1, cx2, cy2)` integrates the cubic by forward
differences — the two interior control points are the only parameters; the
anchors are fixed at `(0,0)` and `(1,1)`, so the curve is a 1-D timing function,
`x ∈ [0,1] → y`. `mel_curve_eval(mode, t, &bez)` dispatches on mode:
`MEL_CURVE_LINEAR` returns `t`, `MEL_CURVE_STEPPED` holds `0`, and
`MEL_CURVE_BEZIER` binary-searches the baked `x` samples and linearly
interpolates `y`. Evaluation is allocation-free and branch-light — the cost the
caller pays is one bake, then table reads (MEL-ENGINE-III).

## Modes

- **`MEL_CURVE_LINEAR`** — identity passthrough; `bez` is ignored (pass `NULL`).
- **`MEL_CURVE_STEPPED`** — constant `0` across the interval (a hold, the
  discrete sibling of `easing`'s `step`); `bez` is ignored.
- **`MEL_CURVE_BEZIER`** — table lookup over a baked `Mel_Bezier`. Endpoints are
  pinned: `t ≤ 0 → 0`, `t ≥ 1 → 1`.

## Relationship to `easing`

`easing` is the closed-form, parameter-free catalogue (`mel_ease_*`); `curve` is
the parametric, baked path. A `cubic_bezier` easing was deliberately excluded
from `easing`'s fixed table (see its `spec.md`) precisely because it belongs
here — this module is where that "not yet" is answered.
