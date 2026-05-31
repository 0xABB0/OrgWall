# 2026-05-31 — color module: split, 8-bit type, and color-science foundation

## Work done — what changed, and why

Three successive asks against `modules/color/`, which began as a single
`color.h` + `color.c` pair (linear-sRGB `mel_color`, HSL/HSV/OKLab/OKLCh, hex,
blend/contrast helpers).

### 1. Split one-type-per-file
`color.c`/`color.h` were decomposed into one translation unit per concern, each
with a matching header under `include/color/`, with `color.h` retained as an
umbrella that includes them all (so existing `<color/color.h>` includers are
untouched). Rationale: the module was about to grow an order of magnitude;
keeping each model isolable makes the growth navigable and lets a caller pull in
one space without the rest.

### 2. 8-bit color type
Added `mel_color8` (`uint8_t` RGBA, sRGB-encoded) in `rgba8`. `mel_color_to_8`/
`from_8` cross the linear↔sRGB boundary (transfer on RGB, alpha left linear).
The pre-existing `mel_color_{to,from}_u32` and `_hex` were re-pointed to
**delegate** through `mel_color8`, so the byte-shuffling logic lives once and the
float and byte paths cannot diverge.

### 3. Deep color-science foundation
Per the spec-first workflow, wrote `modules/color/spec.md` (full architecture
across color science, ergonomics, graphics/HDR, accessibility; no-enum
rationale; failure modes; 4-phase plan), then implemented phase 1:
- `xyz` — CIE XYZ / xyY hub; linear-sRGB ↔ XYZ.
- `space` — `mel_color_space` data descriptor; 10 predefined gamuts; 7 transfer
  functions (sRGB, γ2.2, ProPhoto, Rec.2020, PQ, HLG, linear); `mel_color_convert`
  with automatic Bradford adaptation; per-gamut encoded boundary; typed wrappers
  `mel_p3`/`mel_rec2020`/`mel_aces_cg`.
- `lab`, `luv` — CIE L\*a\*b\*/LCh, L\*u\*v\*/LCh, white-point parameterized.
- `lms` (HPE), `hwb`, `adapt` (Bradford/CAT02/von Kries as distinct functions),
  `difference` (ΔE76/94/2000/OK).

Key design resolutions:
- **MEL-CODE-001 (no enums):** "color space" was split into *models* (distinct
  shapes → distinct C types) and *RGB gamuts* (open set → `mel_color_space`
  data). Adaptation methods are likewise distinct functions, not an enum arg.
- **Naming coherence:** `math`/`core` are PascalCase + `f32` (`Mel_Vec3`,
  `Mel_Mat3`); `color` is lowercase + `float`. Per your decision, `Mel_Vec3`/
  `Mel_Mat3` are used **only inside `.c` files**; the public API stays entirely
  lowercase `mel_*`/`float`, and raw matrices are not exposed — conversion and
  adaptation are offered as operations.

Verification: an 18-check harness (round-trips through every model and gamut,
the D65 white-point XYZ, xyY/LMS/adaptation identities, and reference ΔE values)
compiles against the real `math` headers and passes (exit 0). Math's `Mel_Mat3`
was empirically confirmed row-major with a two-sided inverse before being relied
on.

## Kludges — every shortcut and the debt it leaves

The bar is zero; here is the full account, sanctioned or not.

1. **Verified with `cc`, not `./nob`.** The whole suite was built with direct
   `cc -I…` invocations, never the actual build system. Integration *should* be
   automatic (modules auto-discovered; `math` vec/mat/scalar are header-only
   inline, so no link dependency is introduced), but this is unproven. Debt:
   `./nob test` may surface a discovery or include-path issue I did not see.

2. **Not formatted by `clang-format` (MEL-CODE-004 unmet by me).** `clang-format`
   is absent from this environment, so every file was hand-matched to the repo
   style rather than tool-formatted. (Your editor's linter did reformat
   `src/color_math.h` to one-line bodies afterward; the rest are unverified
   against `.clang-format`.) Debt: a formatting pass on your machine will likely
   produce a diff.

3. **ΔE2000 checked against a single reference pair** (2.0425), not the
   Sharma 34-pair conformance suite. The implementation uses the double-precision
   internals the spec requires, but "passes" rests on one data point plus code
   review, not exhaustive conformance. Debt: a saturated-color or hue-wrap edge
   case could be wrong and the test would not catch it.

4. **`color` is no longer a dependency-free leaf.** It now depends on `math` +
   `core` (your "also depend on math" choice). This is a deliberate, sanctioned
   trade — reuse over isolation — not an accidental coupling, but it is a real
   change to the module's standing recorded here for honesty.

5. **`mel_color_space` is returned by value from constructor functions**
   (`mel_color_space_srgb()` etc.), each rebuilding the descriptor and, in
   `mel_color_convert`, re-deriving the 3×3 RGB↔XYZ matrix and its inverse on
   every call. Correct and allocation-free, but not cached — a hot conversion
   loop pays repeated `mel_mat3_inverse` cost. Acceptable for a foundation;
   flagged against MEL-ENGINE-VI should profiling ever implicate it.

### Process failure (confessed, not a code kludge)
Twice during the session I reported "verified / all checks pass" that were
false — drawn from tool output the environment delivered badly out of order, and
from a shell whose working directory silently reset so `-Iinclude` resolved
against the wrong path. Compounding it, an early corrupted read of the `math`
headers led me to build an entire first pass against a fabricated API
(`mel_vec3`/`mel_mat3`, a false "math has no matrix type" claim, a hand-rolled
inverse with a transpose bug). All of it was corrected once I re-read the headers
cleanly with sentinels and switched to absolute paths + exit-code assertions.
Recorded here because a confident-but-wrong "it passes" is more dangerous than a
visible failure.

## CLAUDE.md suggestions (recommendations only — not applied)

1. **Document the type-naming split.** The repo mixes `Mel_Type`/`f32`
   (`math`, `core`, `allocator`) with `mel_type`/`float` (`color`). A new module
   author cannot infer which to use. A one-line rule in the root or module
   `CLAUDE.md` ("foundational systems modules use `Mel_*`/`f32`; …") would have
   saved this session's largest detour.

2. **State the canonical header include form per module.** Headers are included
   as `<subtree/...>` where the subtree is *not* the module name (`math` exposes
   `math.vector/`, `math.mat/`, `math/`). This is in `platforms.md` but is easy
   to miss; an example in the module `CLAUDE.md` would help.

3. **Note `clang-format` availability.** MEL-CODE-004 mandates formatting, but
   the tool is not present in this environment. Either vendor it into
   `tools/` or note in `CLAUDE.md` that formatting happens via a specific
   path/hook, so an agent knows it cannot self-verify formatting here.

## Suggestions — direction and hygiene

- **Next phase (ergonomics):** `parse`/`format` (CSS Color 4) and `names`
  (named-color table + `nearest_name` via ΔE) are the highest-leverage next
  pieces for `gui`/`display` consumers and have no prerequisite beyond the
  foundation now in place.
- **A real test target.** This module would benefit from entries in the
  synthesized `melody-test` target so the 18-check suite (and a future ΔE2000
  Sharma suite) runs under `./nob test` rather than living in `/tmp`. I wrote
  the checks as throwaway drivers; they should be promoted into the repo.
- **Matrix caching for gamuts** (see kludge 5): if conversion shows up in a
  profile, precompute and store the RGB↔XYZ matrix pair inside
  `mel_color_space` (or a derived handle) rather than re-deriving per call.
- **`gpu`/`display` adoption:** the typed wrappers (`mel_p3`, `mel_color8`) and
  `mel_color_to_8_in` exist precisely so these modules stop carrying ad-hoc
  pixel-packing; a follow-up could migrate one call site as a proof.
