# curve — specification

## Contracts

- `mel_bezier_init(bez, cx1, cy1, cx2, cy2)` bakes a cubic Bézier into `bez`. The
  curve runs from the fixed anchor `(0,0)` to `(1,1)`; `(cx1,cy1)` and
  `(cx2,cy2)` are the two interior control points. `bez` must be non-null
  (asserted, MEL-ENGINE-VIII).
- `mel_curve_eval(mode, t, bez)` is a pure read:
  - `MEL_CURVE_LINEAR` → `t`, unclamped; `bez` may be `NULL`.
  - `MEL_CURVE_STEPPED` → `0` for all `t` (a hold; `t == 1` is `0` by intent, the
    same convention as `easing`'s `step`); `bez` may be `NULL`.
  - `MEL_CURVE_BEZIER` → table lookup; `bez` must be non-null (asserted).
    Endpoints are pinned (`t ≤ 0 → 0`, `t ≥ 1 → 1`); interior `t` binary-searches
    the baked `x` samples and linearly interpolates `y`. A degenerate segment
    (`dx < 1e-7`) returns the segment's `y0` rather than dividing.
- No allocation anywhere; `Mel_Bezier` is a plain value, copyable and trivially
  destructible. The module depends only on `core`.

## Baking

`mel_bezier_init` uses forward differences (the classic finite-difference cubic
evaluator): `n = MEL_BEZIER_SEGMENTS / 2` interior points sampled at parameter
steps `h = 1/(n+1)`, accumulating first/second/third differences so each step is
three adds. The result is `n` `(x,y)` pairs packed into `samples`. Precision is
piecewise-linear across those `n` samples — adequate for timing curves, not a
high-fidelity path evaluator.

## Pre-existing debt (relocated verbatim, not introduced)

The extraction preserves two constructs that sit against the coding guidelines;
they were carried over byte-for-byte from `math` and are flagged here, not fixed,
because the task was a move:

- **`MEL_CURVE_{LINEAR,STEPPED,BEZIER}` are constants modelling a closed set** —
  enum-adjacent (MEL-CODE-001). A redesign would dispatch on something open
  (e.g. a sampler function pointer, the shape `easing` uses), at which point
  `LINEAR`/`STEPPED` collapse into degenerate samplers. Out of scope here;
  requires Gabbo's approval to reshape.
- **`f32 samples[MEL_BEZIER_SEGMENTS]` is a fixed-size array** (MEL-CODE-002).
  The segment count is a baked-resolution constant, not a capacity bound, so it
  is less egregious than a `MEL_MAX_*` cap — but a resolution that the caller
  could choose (a dynamic sample buffer sized at bake) would honor the rule.

## Boundaries / not yet

- Only the cubic Bézier with implicit `(0,0)`/`(1,1)` anchors. No general spline,
  no Catmull-Rom, no arbitrary keyframe track with per-key tangents — those are a
  higher-level animation concern that would consume this module, not live in it.
- The bake resolution (`MEL_BEZIER_SEGMENTS`) is fixed at compile time. A curve
  needing finer sampling cannot request it without changing the constant.
