# color: lazy RGB↔XYZ matrix cache

## Work done

`mel__space_to_xyz` and `mel__space_from_xyz` previously recomputed the full primary/white-point derivation (chromaticity conversions, 3×3 construction, matrix inverse) on every call.  Since the result is fully deterministic from the fixed primaries and white point, it only needed to be computed once per descriptor.

Added three fields to `mel_color_space` in `include/color/space.h`:
- `float to_xyz[9]` — row-major flattened RGB→XYZ matrix
- `float from_xyz[9]` — row-major flattened XYZ→RGB matrix
- `uint8_t matrices_valid` — lazy-init sentinel

Added `mel__space_ensure_matrices(const mel_color_space*)` in `src/space.c`: derives both matrices on the first call per descriptor instance via a `const`-cast write-back, then sets the flag.  `mel__space_to_xyz` / `mel__space_from_xyz` now branch on the flag and copy from the cache rather than recomputing.

The 10 well-known constructors (`mel_color_space_srgb`, etc.) use compound literals that only specify the first 6 fields; C zero-initialises the remainder, so `matrices_valid` starts at 0 and the lazy path fires correctly on first use.

API, return types, and all call sites are unchanged.  Build verified: `./nob build color` — all 15 TUs compiled, `libcolor.a` linked, zero errors.

## Kludges

`mel__space_ensure_matrices` casts `const mel_color_space*` to `mel_color_space*` to write the cache.  This is safe because every caller constructs the struct by value and the struct is mutable at the construction site, but it is UB if a caller ever places a `mel_color_space` in read-only memory (e.g. `static const`).  No such usage exists today.  The proper resolution is `_Atomic uint8_t matrices_valid` for thread-safety and to make the intent explicit, or to expose a `mel_color_space_bake(mel_color_space*)` that callers invoke after construction.

## CLAUDE.md suggestions

None.

## Suggestions

- Consider `mel_color_space_bake(mel_color_space*)` as a named opt-in pre-warm, removing the need for the const-cast pattern and making the cache lifecycle explicit.
- The `matrices_valid` field being a bare `uint8_t` is not thread-safe.  If the module ever gains concurrent access, promote to `_Atomic uint8_t` or guard with a mutex in `mel__space_ensure_matrices`.
- The color module has no test targets (`mel_add_test`).  A round-trip test (sRGB → XYZ → sRGB, sRGB → P3 → sRGB) would catch regressions in the matrix derivation.
