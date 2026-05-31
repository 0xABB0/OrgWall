# color — specification

A deep color module spanning color science, design/dev ergonomics, graphics/HDR,
and accessibility. This document is the contract the implementation follows and
the map of what is built versus sequenced.

## Architecture

### The two-kinds principle (MEL-CODE-001)

A conventional color library hangs everything off a `ColorSpace` enum — a closed
set, forbidden here. The resolution splits "color space" into the two things it
conflates:

- **Models** are distinct geometric shapes — RGB, HSL, HSV, HWB, CIE XYZ, xyY,
  Lab, LCh, Luv, LCh(uv), OKLab, OKLCh, LMS. Each is its own C struct *type*,
  distinguished by the compiler. Adding one is adding a type, not extending an
  enum.
- **RGB gamuts** are an open set of *data*: sRGB, Display-P3, Rec.2020, Adobe
  RGB, ProPhoto, ACEScg, ACES2065-1, and any the user defines. A gamut differs
  from another only by primary chromaticities, white point, and transfer
  functions — so a gamut is a value, `mel_color_space`, not a type and not an
  enum.

The same principle governs operations that would otherwise be enums: chromatic
adaptation methods are distinct functions (`mel_xyz_adapt_bradford`/`_cat02`/
`_von_kries`), not an enum argument.

A handful of common wide-gamut RGB spaces additionally get thin named wrapper
types (`mel_p3`, `mel_rec2020`, `mel_aces_cg`) for compile-time safety at
boundaries — the type-safe face of the same descriptor machinery.

### The hub

All cross-model conversion routes through CIE 1931 XYZ (2° observer). The
canonical working color, `mel_color`, is **scene-linear sRGB / Rec.709
primaries, D65**. `mel_color8` is its 8-bit sRGB-encoded sibling.

```
        mel_color8  ──(transfer)──  mel_color (linear sRGB/D65)
                                          │
                                    (primaries)
                                          │
   Lab/LCh ── Luv/LCh ── xyY ──────── CIE XYZ ──────── LMS
                                          │
                              (chromatic adaptation, Bradford/CAT02)
                                          │
              Display-P3 · Rec.2020 · AdobeRGB · ProPhoto · ACEScg · …
```

`mel_color_space` carries `{ red, green, blue (mel_chromaticity), white,
to_linear, to_encoded }`. From primaries + white the RGB→XYZ matrix derives by
the standard construction (columns = primary XYZ scaled by `S = M⁻¹ ·
white_XYZ`). Conversion between two gamuts is `linear_from → XYZ →
adapt(white_from→white_to) → XYZ → linear_to`; equal white points make the
adaptation matrix identity, so no special case (MEL-ENGINE-IX).

The linear algebra uses `Mel_Mat3` from `math.mat` (row-major, empirically
verified) and `Mel_Vec3` from `math.vector` — **only inside `.c` files**. The
public API stays lowercase `mel_*` over `float`; raw matrices are internal, and
conversion/adaptation are exposed as operations.

## Surface by axis

### Color science  *(foundation — this phase)*
- `xyz`, `xyy`: hub types, `mel_color ↔ XYZ`, `XYZ ↔ xyY`.
- `space`: `mel_color_space`, predefined gamuts, `mel_color_convert`, the
  linear-RGB↔XYZ bridge, encoded-byte boundary per gamut, typed wrappers.
- `lab`: CIE L\*a\*b\* + LCh(ab), white-point parameterized.
- `luv`: CIE L\*u\*v\* + LCh(uv).
- `lms`: cone-response space (HPE), the substrate for adaptation and CVD.
- `adapt`: Bradford / CAT02 / von Kries chromatic adaptation.
- `difference`: ΔE76, ΔE94 (graphic-arts), ΔE2000 (double-precision internals),
  ΔE-OK.
- *Sequenced:* correlated color temperature (Planckian locus, McCamy), blackbody
  → color, optional spectral (CMF integration, Kubelka-Munk subtractive mixing).

### Design & dev ergonomics  *(sequenced)*
- `parse` / `format`: full CSS Color 4 — `#hex`, `rgb()/rgba()`, `hsl()`,
  `hwb()`, `lab()/lch()`, `oklab()/oklch()`, `color(<space> …)`, named colors.
  Pure value functions; serialization writes caller buffers and returns the
  required length (the `to_hex` contract).
- `names`: CSS Color 4 named table, `from_name`, `nearest_name` (ΔE search).
- `harmony`: complementary, analogous, triadic, tetradic, split-complementary,
  monochromatic — emit into a `mel_array` (MEL-CODE-002/003: allocator-driven).
- `scale`: sequential / diverging / categorical palette generation, sampling an
  interpolation in any model; cubehelix; allocator-driven.

### Graphics & HDR  *(gamuts in foundation; rest sequenced)*
- Wide-gamut descriptors with PQ (ST 2084) and HLG (B67) transfer — *foundation*.
- `gamut`: `in_gamut`, hard `clip`, CSS Color 4 OKLCh chroma-reduction mapping.
- `blend`: separable (multiply, screen, overlay, darken, lighten, dodge, burn,
  hard/soft light, difference, exclusion) and non-separable (hue, saturation,
  color, luminosity) — each a `mel_color (*)(mel_color backdrop, mel_color src)`
  function plus a generic `mel_blend(b, s, fn)` (open set as functions, not an
  enum; MEL-CODE-001).
- `composite`: Porter-Duff (over/in/out/atop/xor/plus/clear/src/dst) as
  functions, straight and premultiplied alpha.
- `tonemap`: Reinhard, extended Reinhard, ACES filmic (Narkowicz), AgX,
  Uncharted-2; per-channel and luminance-only.

### Accessibility  *(sequenced)*
- `contrast`: WCAG 2.1 ratio (present in `rgba`), APCA Lc.
- `cvd`: protan/deutan/tritan simulation (Brettel via LMS; Machado matrices),
  severity-parameterized; accessible-pair search built on ΔE + contrast.

## Failure modes addressed
- **Divide-by-zero** in Luv (`X+15Y+3Z=0`), Lab/Luv inverse at `L=0`,
  `over`/`composite` at `a=0`, xyY at `x+y+z=0`: each guarded, returning the
  defined limit, never NaN (MEL-ENGINE-VIII).
- **Precision** in ΔE2000 (`C⁷` overflows float range for saturated colors):
  internals in `double`, result in `float`.
- **Out-of-gamut** after conversion: representable as negative/`>1` linear
  components (legal, HDR/wide-gamut intent); narrowing to a display happens only
  through explicit `gamut` mapping, never silently.
- **White-point mismatch**: adaptation is automatic in `mel_color_convert`; the
  identity case is the equal-white-point limit, not a branch.

## Phase plan
1. **Foundation (this phase):** `color_math` (internal), `xyz`, `space`, `lab`,
   `luv`, `lms`, `hwb`, `adapt`, `difference`.
2. Ergonomics: `names`, `parse`/`format`, `harmony`, `scale`.
3. Graphics/HDR: `gamut`, `blend`, `composite`, `tonemap`.
4. Accessibility & science tail: `contrast` (APCA), `cvd`, `temperature`,
   spectral.

Every phase ships only complete, tested pieces — no stubs (MEL-ENGINE-VIII).
