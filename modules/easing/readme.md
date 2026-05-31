# easing

Normalized easing curves — the Penner family that every tween, transition, and
camera move re-derives. One `f32 -> f32` shape per name, mapping unit time to
unit progress. Extracted from `math`, where the curves crowded the scalar
vocabulary they merely borrow from. Namespace `<easing/...>`, symbol prefix
`mel_ease_`.

## Why

Easing is animation's lingua franca: UI shows in/out, sprites overshoot with
back, springs settle with elastic, menus drop with bounce. The curves are pure
functions of normalized time, not arithmetic primitives, so they do not belong
beside `min`/`clamp`/`lerp` in `math/scalar`. Splitting them lets the animation
and gui layers depend on the easing vocabulary alone, and lets the set grow
(MEL-ENGINE-IX) without thickening `math`. The bodies are unchanged; only their
home moved.

## Model

Every easing is `MEL_NODISCARD static inline f32 mel_ease_<name>(f32 t)` over
`t ∈ [0,1]`, returning progress that begins at 0 and ends at 1 (the back and
elastic families overshoot in between by design). Header-only: the curves are a
handful of FLOPs and inline straight into the caller's loop — the simple path
and the fast path are the same path (MEL-ENGINE-II). The transcendental
families (`sine`, `circ`, `expo`, `elastic`, plus the `in_out` halves) lean on
`mel_powf`/`mel_sinf`/`mel_cosf`/`mel_sqrtf` and `MEL_PI`/`MEL_TAU`/`MEL_HALF_PI`
from `<math/scalar.h>`, which is why this module depends on `math` (a
compile-time include dependency only — scalar is itself header-only over
compiler builtins, so nothing links).

## Curves

`linear`; `in`/`out`/`in_out` for `quad`, `cubic`, `quart`, `quint`, `sine`,
`circ`, `expo`, `elastic`, `back`, `bounce`; the two graphics-heritage sigmoids
`in_out_smooth` (Hermite `t²(3−2t)`, delegating to `mel_smoothstepf`) and
`in_out_smoother` (Perlin `6t⁵−15t⁴+10t³`); and a degenerate `step` (holds 0
across the interval). The polynomial families are bare multiplies; `bounce`
composes from `out_bounce`; `back` and `elastic` carry their canonical
overshoot/period constants inline. The two `smooth` curves clamp their input to
`[0,1]` (the GLSL `smoothstep` heritage) — unlike the Penner curves, which
extrapolate.

## Registry

`<easing/easing.h>` also exposes a name→function table for data-driven
selection: `Mel_Easing_Func` (the `f32 (*)(f32)` shape), `Mel_Easing_Entry`
(`{ name, func }`), and the `MEL_EASING_LIST(X)` X-macro that enumerates all 34
curves so a caller can build a lookup, a dropdown, or a serializer without
restating the set. `MEL_EASING_COUNT` is its cardinality.
