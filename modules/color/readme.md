# color

Color types, spaces, and conversions: representation, color-science conversion,
gamut handling, and perceptual difference for `gui`, `display`, and any app that
renders. See `spec.md` for the full architecture and the phased build plan.

## Why

Rendering code needs a correct, shared color vocabulary. Folding ad-hoc
`float[4]`s, hand-rolled hex parsing, and per-call-site gamma mistakes into every
caller is how blending and color bugs multiply. This module owns that vocabulary
once — from the byte on the wire to CIE XYZ and back.

## Model

`mel_color` is the canonical working type: **scene-linear sRGB / Rec.709
primaries, D65**, components in `[0,1]` with HDR values above `1` permitted.
Blending, interpolation, luminance, and OKLab live in linear light, the only
space in which they are correct. `mel_color8` is its 8-bit sRGB-encoded sibling.

### Two kinds, never an enum (MEL-CODE-001)

A `ColorSpace` enum is a closed set, so it is forbidden. "Color space" is split
into the two things it conflates:

- **Models** are distinct shapes and so are distinct C *types*: `mel_color`,
  `mel_color8`, `mel_hsl`, `mel_hsv`, `mel_hwb`, `mel_xyz`, `mel_xyy`, `mel_lab`,
  `mel_lch`, `mel_luv`, `mel_lchuv`, `mel_oklab`, `mel_oklch`, `mel_lms`.
- **RGB gamuts** are an open set of *data*: a `mel_color_space` value carries
  primary chromaticities, white point, and a transfer-function pair. Conversion
  between gamuts (`mel_color_convert`) routes through XYZ with automatic Bradford
  chromatic adaptation; equal white points make adaptation the identity, with no
  special case. Predefined: sRGB, linear sRGB, Display-P3, Rec.2020 (+ PQ, HLG),
  Adobe RGB, ProPhoto, ACEScg, ACES2065-1. Define your own by value.

A few wide-gamut spaces also get thin named wrapper types (`mel_p3`,
`mel_rec2020`, `mel_aces_cg`) for type-safety at boundaries.

8-bit and hex (`mel_color_to_u32`/`from_u32`, `mel_color_to_hex`/`from_hex`)
treat their wire form as sRGB `0xRRGGBBAA`. `mel_color_to_8_in`/`from_8_in`
generalize that encoded boundary to any `mel_color_space`. Alpha is never
gamma-encoded.

## Layout

One concern per translation unit, each with a matching header under
`include/color/`:

- `rgba`, `rgba8` — `mel_color`, its operations, the sRGB transfer, and the
  8-bit type with packing/hex.
- `hsl`, `hsv`, `hwb` — cylindrical models over sRGB-encoded values.
- `oklab`, `oklch` — perceptual models in linear light.
- `xyz` — the CIE XYZ / xyY hub and the linear-sRGB ↔ XYZ bridge.
- `space` — `mel_color_space`, predefined gamuts, `mel_color_convert`, the
  `mel_linear_rgb_to_xyz`/`mel_xyz_to_linear_rgb` bridge, the encoded-byte
  boundary, and the typed wrappers.
- `lab`, `luv` — CIE L\*a\*b\*/LCh(ab) and L\*u\*v\*/LCh(uv), white-point
  parameterized.
- `lms` — cone-response space (HPE), substrate for adaptation and CVD.
- `adapt` — chromatic adaptation: `mel_xyz_adapt_bradford`/`_cat02`/`_von_kries`.
- `difference` — ΔE76, ΔE94, ΔE2000 (double-precision internals), ΔE-OK.

`color.h` is an umbrella including them all; reach for an individual header to
pull in one model. Internal helpers live in `src/color_internal.h` (saturation)
and `src/color_math.h` (matrix/vector builders over `math`).

## Dependencies

`math` (`Mel_Vec3` from `math.vector`, `Mel_Mat3` from `math.mat`, scalar
helpers — all header-only) and `core` types transitively, plus libc `<math.h>`.
The RGB↔XYZ and chromatic-adaptation linear algebra uses `math`'s row-major
`Mel_Mat3`, but **only inside `.c` files**. The public API is entirely lowercase
`mel_*` over `float`; no `Mel_Vec3`/`Mel_Mat3` crosses a header, and raw matrices
are not exposed — conversion and adaptation are offered as operations.

## Contract

Conversions and operations are pure value functions; they allocate nothing.
Divide-by-zero limits (Luv at `X+15Y+3Z=0`, Lab/Luv inverse at `L=0`, `over` at
`a=0`, xyY at `x+y+z=0`) are guarded — the defined limit, never NaN
(MEL-ENGINE-VIII). Out-of-gamut results after conversion are representable as
negative or `>1` linear components and are narrowed to a display only through
explicit gamut mapping, never silently. `mel_color_to_hex`/`to_8` and friends
write into caller buffers and report the length required.

## Status

Foundation (color science: hub, spaces, adaptation, Lab/Luv/LCh, ΔE) is built
and numerically verified (18-check round-trip + reference-value suite). Sequenced
next, per `spec.md`: CSS Color 4 parse/format, named colors, harmony and
palette/scale generation (allocator-driven), gamut mapping, blend modes,
Porter-Duff compositing, tone mapping, APCA contrast, and CVD simulation.
