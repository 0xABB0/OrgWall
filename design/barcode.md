# Melody Barcode — `barcode`

Symbol generation and (sequenced) recognition for 1D and 2D barcodes. The
abstraction is *payload bytes → a grid of dark/light modules*; everything a
caller needs to put a scannable mark on screen, on paper, or on a part. Output
stops at the module grid — rasterization, color, and print are `gui`/`gpu`/
`color`'s job (MEL-ENGINE-IX). Recognition (camera/scanner buffer → bytes) is
the harder inverse and is sequenced, not refused (MEL-ENGINE-I).

This document is bound by the Ten Commandments of the Engine. Where a decision
turns on one, it is cited by tag (`MEL-ENGINE-N`) or coding rule (`MEL-CODE-NNN`).

---

## 1. Module identity

Top-level module `barcode`. No platform axis: encoding is pure compute over
bytes, so the whole encode surface is portable C with zero syscalls — it runs
identically on every target, web included. Decode, when it lands, also stays
pure: it consumes a caller-supplied grayscale buffer and never opens a camera
itself; capture belongs to `sensor`/platform, keeping `barcode` a transform, not
a device (MEL-ENGINE-IX).

Dependencies: `core` (types), `allocator` (every buffer is allocator-backed,
MEL-CODE-003), `collection.array` for growable working buffers. No `math`, no
`gpu`. libc `<string.h>` only.

---

## 2. The closed-set problem (MEL-CODE-001)

The instinct is `enum Symbology { QR, EAN13, CODE128, … }` with a dispatcher.
That is a closed set, forbidden. As `color` split "color space" into *models*
(types) and *gamuts* (data), "symbology" splits into three things it conflates:

- **Family geometry** is a *shape*, and the two shapes collapse to one output
  type. A 1D symbology is a row of bars; a 2D symbology is a grid of modules. A
  row is a grid of height 1. So both encoders emit the single canonical output,
  `mel_barcode_matrix` — no tag distinguishes them; `height == 1` *is* linear.
  One boundary type, no special-casing downstream (MEL-ENGINE-IX).

- **Each symbology is a distinct procedure**, hence a distinct *function*, not an
  enum arm: `mel_qr_encode`, `mel_ean13_encode`, `mel_code128_encode`,
  `mel_datamatrix_encode`, … Exactly the pattern by which `color` exposes
  adaptation methods as `mel_xyz_adapt_bradford/_cat02/_von_kries` rather than an
  enum argument. Adding a symbology adds a function and a translation unit;
  nothing existing is touched, no central switch grows an arm.

- **Per-family parameters are open data**, carried in a small per-symbology
  options struct passed by value (designated-initializer call sites, the house
  idiom seen in `mel_guard_init`). QR's version, ECC target, and mask are fields;
  `0`/`-1` means *auto* and the encoder chooses optimally. Code128's code set is
  chosen by the encoder, not demanded of the caller.

### The error-correction-level wrinkle

QR's four ECC levels (L/M/Q/H) look like the textbook enum. They are modeled as
`color` models gamuts: an open descriptor of *recovery capacity*, with four
predefined values exported as functions —

```
mel_qr_ecc mel_qr_ecc_l(void);   // ~7%  recovery
mel_qr_ecc mel_qr_ecc_m(void);   // ~15%
mel_qr_ecc mel_qr_ecc_q(void);   // ~25%
mel_qr_ecc mel_qr_ecc_h(void);   // ~30%
```

The encoder snaps a requested recovery fraction to the nearest standard level the
spec permits. A caller states intent (*how much damage must this survive*), not a
magic constant; the closed set the QR spec fixes stays an implementation detail,
never an API enum.

---

## 3. The boundary type — `mel_barcode_matrix`

```
typedef struct mel_barcode_matrix {
    u8*  modules;          // 1 byte/module, row-major; 1 = dark, 0 = light
    i32  width;            // modules across (content only, no quiet zone)
    i32  height;           // module rows; 1 ⇒ linear
    i32  quiet_zone;       // advised quiet-zone width in modules
    const Mel_Alloc* allocator;
} mel_barcode_matrix;
```

Decisions and their justification:

- **Byte per module, not a bitset.** The largest symbol (QR v40, 177²) is ~31 KiB
  — negligible — and a byte grid makes `get(x,y)` a load, not a bit-extract,
  which the rasterizer hits once per output texel. Density would buy nothing and
  cost the hot path (MEL-ENGINE-VI: respect the device that *renders*, not vanity
  compaction). A packed view is sequenced behind `collection.set.bitset` only if
  a profile demands it.

- **Quiet zone is advised, never painted.** The grid is content; the quiet zone
  is a rasterizer concern (its color is the *light* color, which `barcode` does
  not know). Baking it in would couple module geometry to background color and
  bloat every matrix — a stolen decision (MEL-ENGINE-III, -IX). The field carries
  the symbology's spec minimum (EAN: 9–11 left / 7 right; Code128/39: 10; QR: 4)
  so the caller need not look it up.

- **`height == 1` is the linear marker**, not a flag. A linear encoder emits one
  row; the caller chooses presentational bar height by stretching at raster, or
  by passing a `height` to the encoder, which replicates the row. No 1D/2D enum
  (MEL-CODE-001), no derivable-state flag (MEL-ENGINE-IX).

- **Owns its allocator**, mirroring `Mel_Array`. `init` allocates `width*height`
  zeroed bytes; `free` releases and zeroes the struct. Every encoder takes the
  destination matrix and an allocator; nothing is claimed in secret
  (MEL-ENGINE-III).

Accessors: `init/free/get/set`, plus `set_module` and a column helper the linear
painters use. `get`/`set` assert bounds in debug, the merciless contract
(MEL-ENGINE-VIII).

---

## 4. Encode pipelines

### 4.1 Linear family — *foundation, this phase*

Linear symbologies need only the matrix; no error-correction algebra. They share
one internal shape: a **run painter** that appends bar/space runs of a given
module width across row 0, advancing a cursor. Total module width is summed
first, the matrix is sized exactly, then painted — no fixed buffers
(MEL-CODE-002), no over-allocation.

- **Code 39** — self-checking, 9 elements/char (3 wide), narrow:wide ratio fixed
  at 1:3 in modules, `*` start/stop, 43-symbol set, optional mod-43 check digit.
  The thinnest end-to-end slice; it proves the painter and the boundary type with
  no table heavier than 44 patterns.
- **EAN-13 / EAN-8 / UPC-A** — fixed structure, guard patterns (`101`/`01010`),
  7-module digit cells. EAN-13's first digit is *not* drawn; it selects the
  L/G parity word of the left group — the subtle bit, verified against a known
  vector. Check digit is mod-10 (3-weight). UPC-A is EAN-13 with an implicit
  leading `0`; UPC-E compression is sequenced.
- **ITF (Interleaved 2 of 5)** — digit pairs interleave 5 bars (digit A) with 5
  spaces (digit B); requires even length (the encoder pads or errors per option);
  optional mod-10 check.
- **Code 128** — the full 107-entry pattern table (each symbol 11 modules: 3
  bars + 3 spaces summing to 11), start A/B/C, mod-103 checksum, 13-module stop.
  The encoder auto-selects code C across digit runs (≥4, or ≥2 at an end) and
  code B otherwise — the table is the bulk, the switcher ~30 lines. GS1/FNC1
  application identifiers are sequenced.

Each linear encoder validates its alphabet and length, returning a clear error
(not a corrupt symbol) on violation (MEL-ENGINE-VIII).

### 4.2 2D family — *sequenced, next phases*

All four 2D symbologies share two prerequisites that have **no prerequisites of
their own** and are therefore the foundation of this phase:

- **`bitwriter`** — an MSB-first bit serializer over an allocator-backed byte
  buffer: `put(value, bit_count)`, `pad_to_byte`, `bytes`. QR/DataMatrix/Aztec
  assemble their codeword stream bit by bit; the linear family does not need it,
  which is why it is 2D-phase, not foundation-wide.

- **`galois` / Reed–Solomon** — a *field-parameterized* RS coder, not four copies.
  QR, DataMatrix, and Aztec are `GF(2⁸)` (differing primitive polynomials);
  PDF417 is `GF(929)`, a prime field. The coder takes the field (`p`, `k`,
  primitive) as *data* (MEL-CODE-001 again — a field is a value, not an enum) and
  generates `n` ECC symbols for a message. One verified algebra serves all four.

Then, per symbology, the placement/masking machinery:

- **QR** (model 2, v1–40): segmentation (optimal numeric/alphanumeric/byte/kanji
  mode runs), data encoding, RS interleaving, function-pattern placement (finder,
  separators, timing, alignment, format & version info), 8-pattern masking with
  penalty scoring. Micro-QR sequenced.
- **DataMatrix** (ECC 200): ASCII/C40/Text/Base256 encodation, RS, the diagonal
  placement walk, finder "L" + timing, square and rectangular sizes.
- **Aztec**: bit encodation, RS, concentric bullseye, mode message, compact and
  full ranges.
- **PDF417**: byte/text/numeric compaction, `GF(929)` RS, row/column structure,
  start/stop and row indicators. Truncated PDF417 sequenced.

### 4.3 Decode — *sequenced, last*

Grayscale buffer → bytes. Binarization (adaptive threshold), finder-pattern
detection per family, perspective rectification (homography), sampling to a
module grid, RS *erasure+error* correction, payload decode. The RS coder from
4.2 runs in reverse (syndromes, Berlekamp–Massey, Chien, Forney). Capture is
never owned here (MEL-ENGINE-IX); a `mel_image_gray` view is the input contract.

---

## 5. Failure modes, walked

- **Empty / over-long payload** → `init` not called, encoder returns `false` with
  the matrix untouched; no zero-size allocation, no partial symbol.
- **Out-of-alphabet character** (e.g. lowercase to Code 39, non-digit to EAN) →
  rejected before any allocation, honest `false` (MEL-ENGINE-VIII), not a
  silently mangled symbol that scanners read as garbage.
- **Wrong fixed length** (EAN-13 given 11 digits) → reject, *unless* the missing
  char is exactly the check digit, which `*_with_checkdigit` helpers compute and
  append; the plain encoder demands the full, correct length and verifies the
  supplied check digit.
- **Odd-length ITF** → option decides: pad a leading `0` or reject; default
  reject, because a silent pad changes the encoded number (MEL-ENGINE-VIII).
- **QR capacity exceeded** at the chosen version → encoder either bumps the
  version (auto) or, if the caller pinned a version too small, returns `false`
  with the required version reported, never a truncated payload.
- **Allocator failure** → propagated as `false`; the matrix is left freed/zeroed,
  no leak of a half-built grid (the `mel_array_free` discipline).
- **`GF(929)` vs `GF(2⁸)` mismatch** — the field is carried in the coder value,
  so a PDF417 message can never be fed the QR field; the type makes the error
  unconstructible rather than asserted late.

---

## 6. Phasing & the no-prerequisite-first rule

1. **Foundation (this phase):** `matrix` + the linear family (Code 39, EAN-13/8,
   UPC-A, ITF, Code 128), verified against published test vectors. This slice has
   no prerequisite beyond `allocator`/`collection` and is immediately useful.
2. **2D substrate:** `bitwriter`, then the field-parameterized `galois`/RS coder,
   each unit-tested in isolation before any symbology consumes them.
3. **2D symbologies:** QR first (highest demand), then DataMatrix, Aztec, PDF417
   — each its own translation unit and header, landing on its own merit.
4. **Decode:** binarization → finders → rectify → RS-correct → payload, QR first.

Nothing here declares a capability impossible; every deferral is *not yet*
(MEL-ENGINE-I). The order is strict dependency order: the foundation borrows from
no later phase.
