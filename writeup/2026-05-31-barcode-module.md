# 2026-05-31 — barcode module: linear family

## Work done

New top-level module `barcode`: payload bytes → a grid of dark/light modules, the
caller rasterizing via `gui`/`gpu`/`color`. Encode-first; decode and 2D sequenced.

- **Design & spec.** `design/barcode.md` (failure-mode walk, the no-symbology-enum
  resolution à la `color`'s gamuts, the module-grid boundary, phasing),
  `modules/barcode/{readme.md,spec.md}`.
- **`matrix`** — `mel_barcode_matrix`: row-major byte grid (`1` dark / `0` light),
  allocator-owned, advisory quiet zone never painted, `height == 1` ⇒ linear.
- **Linear family**, each its own translation unit + header, no symbology enum
  (distinct `mel_<sym>_encode` functions, MEL-CODE-001):
  - `ean` — EAN-13/EAN-8/UPC-A: guard patterns, L/G/R cells, first-digit parity
    selection, right-anchored mod-10 check (accepts data ± check digit).
  - `code39` — 43-symbol set + `*` start/stop, 1:3 narrow:wide, optional mod-43.
  - `itf` — Interleaved 2 of 5, digit-pair interleave, even-length, optional
    mod-10, opt-in odd padding.
  - `code128` — full 107-pattern table, Start B/C, mod-103 checksum, auto Code-C
    over digit runs, 13-module stop.
- **Tests** wired into `melody-test`: `test.{ean,code39,itf,code128}.c`.

### Verification

- EAN check digits against independent vectors (`590123412345→7`,
  `400638133393→1`, `9638507→4`) — table-independent, caught an off-by-one in the
  weight anchor (must anchor weight-3 at the rightmost data digit: EAN-13 weights
  odd-from-left as 1, EAN-8 as 3; `n-1-i` unifies both).
- ITF digit table proven correct via the 1-2-4-7-parity weight formula (each
  digit's wide-element weights sum to the digit, 0↔11).
- Code 39 / Code 128 glyph tables hand-audited against their structural laws
  (Code 39: exactly 3 wide, 2 wide-bars + 1 wide-space except `$ / + %`; Code 128:
  every symbol's elements sum to 11 with even total bar width). Full 95/47/27/46-
  module single-symbol anchors pin the absolute mapping and the painter.
- A **table-independent** structural decode of a real Code 128 output
  (`Hello128!`) confirms every painted symbol sums to 11 (stop 13) with even bar
  parity.
- Clean compile under `-std=c23 -Wall -Wextra`; all four test TUs syntax-check
  against the real `test/test.h`.

## Kludges & debt (MEL-ENGINE-VIII — confess all)

1. **Code 128 is Sets B + C only.** Set A (control chars 0–31) is unsupported;
   input is restricted to printable ASCII 32–126, others rejected. FNC1/GS1
   application identifiers absent. The B↔C switch heuristic is greedy (switch to C
   on a digit run ≥4, or ≥2 at an end), near-optimal but **not** provably
   minimal-length. Sequenced, not refused (MEL-ENGINE-I); confessed here.
2. **Glyph tables are canonical-sourced, not externally vector-verified in CI.**
   Code 128's 107 patterns and Code 39's char↔pattern mapping come from the
   published tables. They pass structural invariants (sum, parity, wide-count,
   distinctness), single-symbol anchors, and a structural decode — but a
   transcription error that preserved *every* invariant would slip through. The
   honest residual: no third-party scanner/reference-symbol check runs in CI.
   (ITF and EAN are fully arithmetic-verified and carry no such residual.)
3. **`./nob test` does not complete** — pre-existing `continuation` codegen
   breakage (missing `*.cont.h`, the pass hasn't run) aborts the shared
   `melody-test` link before barcode's TUs. Not introduced here, but it means the
   wired barcode tests are verified only via a standalone harness + syntax-check,
   not the real test runner. Debt against the repo, surfaced.
4. **ITF `pad_odd` prepends `'0'`**, altering the encoded number. Conventional,
   opt-in, default-reject — but a caller that pads blindly changes their data.
5. **Quiet zone is a single advisory scalar.** EAN-13's asymmetric 11-left/7-right
   collapses to one number (11); fidelity lost at the boundary, by design.
6. **`mel__painter` is defined twice** — in `src/paint.h` (run painter, used by
   code39/itf/code128) and locally in `ean.c` (bit painter, predates paint.h).
   Separate TUs, no ODR issue, but `ean.c` could fold onto the shared painter.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document how to run a **single module's** tests. The shared `melody-test` target
  is fragile: one module's broken codegen (`continuation` today) masks every other
  module's tests. A `./nob test <module>` or filter would let a module be verified
  in isolation.
- A line on the codegen contract: which targets require a codegen pass before
  `nob test`, and how to trigger it, so a missing `*.cont.h` is diagnosable
  without spelunking.

## Suggestions

- **Next slice (2D substrate):** `bitwriter` (MSB-first serializer) then the
  field-parameterized Reed–Solomon coder (`GF(2⁸)` for QR/DataMatrix/Aztec,
  `GF(929)` for PDF417) — both no-prerequisite leaves, unit-tested before any
  symbology consumes them. Then QR.
- **Close the verification residual (debt #2):** add a one-off offline check that
  cross-references the Code 128/39 tables against a generated reference (e.g. a
  vetted third-party encode for a handful of strings) and bakes the expected
  module strings in as anchors. Turns "structurally plausible" into "vector-true".
- **Repo hygiene:** the `continuation` codegen breakage (debt #3) blocks the whole
  test suite — worth fixing or guarding so `nob test` is green again.
- **Compose forward:** a `barcode`→`gui`/`gpu` raster example (module grid +
  quiet zone → texture) would demonstrate the boundary and exercise MEL-ENGINE-IX.

---

## Addendum — 2D substrate (`bitwriter`, `galois`, `rs`)

Same session, next phase: the prerequisites every 2D symbology shares, built and
verified before any consumer exists.

- **`bitwriter`** — MSB-first serializer over a `Mel_Array(u8)`: `put(v,bits)`,
  `pad_to_byte`, `bit_length`, `bytes`. The linear family paints at the module
  level and never needed it; QR/DataMatrix/Aztec assemble codewords bit by bit.
- **`galois`** — a field modeled as **open data carrying its arithmetic as
  function pointers** (the `Mel_Alloc` callback pattern), so no enum or tag
  selects the field family (MEL-CODE-001). `mel_gf_binary_init` builds `GF(2ᵐ)`
  with log/exp tables and a per-symbology primitive polynomial; `mel_gf_prime_init`
  builds `GF(p)` (929 for PDF417). `add`/`sub`/`mul`/`pow`/`inv`, the last two
  generic over both families via square-and-multiply / Fermat.
- **`rs`** — `mel_rs_generate(field, alpha, first_root, …)`: one Reed–Solomon
  coder over the field pointers, sign-correct `(x − root)` generator.

### Verification — the residual the glyph tables couldn't close, closed

- `GF(2⁸)`/0x11D: full `exp[log[a]]==a` and `a·a⁻¹==1` sweeps; `α·0x80==0x1D`.
- `GF(929)`: `3·310==1`, `inv(3)==310`, and `a·a⁻¹==1` across all 928 nonzeros.
- **External reference:** the canonical QR "HELLO WORLD" 16 data codewords →
  exactly the published 10 EC codewords `{196,35,39,119,235,215,231,226,93,23}`,
  and the degree-10 generator's α-exponents match `{0,251,67,46,61,118,70,64,94,
  32,45}`.
- **Field-agnostic invariant:** a generated codeword `data‖ecc` evaluates to 0 at
  every generator root — asserted for both `GF(2⁸)` and `GF(929)`. This needs no
  reference table, so the RS coder carries **no** unverified residual (unlike the
  Code 39/128 glyph tables).

### Bug caught (by the invariant, not the reference)

Systematic RS ECC is the **negated** remainder: a codeword is `data·xⁿ − r`, so
`ecc = −r`. In `GF(2ᵐ)` negation is identity — HELLO WORLD passed regardless and
hid it — but in `GF(929)` the codeword failed to vanish at its roots. The fix
(`ecc[i] = sub(0, rem[…])`) is identity on binary and `929 − r` on prime, which is
precisely PDF417's definition. Verifying only against the binary reference would
have shipped a broken prime-field coder; the algebraic invariant is what caught
it.

### Debt unchanged

`./nob test` still blocked upstream by `continuation` codegen; the substrate was
verified via standalone harness + syntax-check against the real `test.h`. No new
kludges. Next: QR atop this substrate.
