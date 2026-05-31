# barcode — specification

Symbol generation for 1D and 2D barcodes, plus sequenced recognition. This
document is the contract the implementation follows and the map of built versus
sequenced. The design rationale (failure-mode walk, justifications) lives in
`design/barcode.md`; this is the surface.

## Principle — no symbology enum (MEL-CODE-001)

"Symbology" splits into three things it conflates:

- **Geometry** collapses to one output type. 1D is a row, 2D a grid, a row is a
  grid of height 1 — so every encoder emits `mel_barcode_matrix` and `height == 1`
  *is* linear. No 1D/2D tag.
- **Each symbology is a distinct function**, `mel_<sym>_encode`, not an enum arm
  — adding one adds a translation unit and touches nothing existing.
- **Per-family parameters are open data** on a by-value options struct; `auto`
  sentinels (`0`/`-1`) let the encoder choose optimally. QR's ECC level is an
  open recovery-capacity descriptor with four predefined values, not an enum
  (the `color`-gamut pattern).

## The boundary type

```
typedef struct mel_barcode_matrix {
    u8*  modules;          // 1 byte/module, row-major; 1 = dark, 0 = light
    i32  width, height;    // modules; height == 1 ⇒ linear
    i32  quiet_zone;       // advised quiet-zone width in modules
    const Mel_Alloc* allocator;
} mel_barcode_matrix;

bool mel_barcode_matrix_init(mel_barcode_matrix* m, i32 w, i32 h, const Mel_Alloc* a);
void mel_barcode_matrix_free(mel_barcode_matrix* m);
bool mel_barcode_matrix_get(const mel_barcode_matrix* m, i32 x, i32 y);
void mel_barcode_matrix_set(mel_barcode_matrix* m, i32 x, i32 y, bool dark);
void mel_barcode_matrix_fill_column(mel_barcode_matrix* m, i32 x, bool dark);
```

Byte per module (not a bitset): the grid is tiny and `get` is the rasterizer hot
path. Quiet zone advised, never painted. The matrix owns its allocator; `free`
zeroes the struct.

## Surface by family

### Linear  *(foundation — this phase)*

Every linear encoder takes `(mel_barcode_matrix* out, const char* data, i32
height, const Mel_Alloc* a)` plus a per-symbology options struct, sums the module
width, sizes the matrix exactly, and paints row 0 (replicated to `height`). All
validate before allocating.

- `code39`: `mel_code39_encode`, opt `{ bool checksum; }`. 43-symbol set, `*`
  start/stop, 1:3 narrow:wide, optional mod-43 check.
- `ean`: `mel_ean13_encode`, `mel_ean8_encode`, `mel_upca_encode`. Guard
  patterns, L/G/R digit cells, first-digit parity selection (EAN-13), mod-10
  check. `*_with_checkdigit` variants append the check digit. UPC-E sequenced.
- `itf`: `mel_itf_encode`, opt `{ bool checksum; bool pad_odd; }`. Digit-pair
  interleave, even-length, optional mod-10.
- `code128`: `mel_code128_encode`. Full 107-pattern table, start A/B/C, mod-103
  checksum, auto code-C over digit runs. GS1/FNC1 sequenced.

### 2D substrate  *(built)*

- `bitwriter`: MSB-first bit serializer over an allocator-backed buffer —
  `put(v, bits)`, `pad_to_byte`, `bytes`, `bit_length`.
- `galois`: a field is open data carrying its arithmetic as function pointers
  (the `Mel_Alloc` pattern, never an enum/tag — MEL-CODE-001). Two builders:
  `mel_gf_binary_init` (`GF(2ᵐ)`, log/exp tables, per-symbology primitive) and
  `mel_gf_prime_init` (`GF(p)`, e.g. 929). `add`/`sub`/`mul`/`pow`/`inv`.
- `rs`: `mel_rs_generate(field, alpha, first_root, data, n_data, n_ecc, out)` —
  one coder over the field pointers, sign-correct `(x − root)` generator so the
  emitted ECC is the negated remainder (identity in `GF(2ᵐ)`, `p − r` in `GF(p)`).
  One algebra, four consumers. Verified against the QR reference codewords and the
  field-agnostic invariant that a codeword vanishes at every generator root.

### 2D symbologies  *(sequenced)*

- `qr` (model 2): ECC level as an open recovery descriptor (`mel_qr_ecc_l/m/q/h`,
  never an enum). **Codeword stage built** (v1–10): mode detection
  (numeric/alphanumeric/byte), bitstream, terminator + EC/11 padding, per-block
  Reed–Solomon, data/EC interleave — `mel_qr_codewords` returns the interleaved
  stream + resolved version. Verified byte-exact against the ISO/IEC 18004
  `01234567` 1-M example (data *and* EC codewords) plus the codeword-root
  invariant. **Sequenced:** geometry — function-pattern placement, zigzag data
  walk, 8-mask penalty selection, format/version info → `mel_barcode_matrix` via
  `mel_qr_encode`; then versions 11–40, Kanji/ECI, Micro-QR.
- `datamatrix` (ECC 200): ASCII/C40/Text/Base256, RS, diagonal placement, square
  + rectangular.
- `aztec`: bullseye, mode message, compact + full.
- `pdf417`: text/byte/numeric compaction, `GF(929)` RS, row/column structure.

### Decode  *(sequenced — last)*

`mel_image_gray` view → bytes: binarize, find, rectify (homography), sample, RS
erasure+error correct, decode. Capture is never owned here; the input is a
caller buffer (MEL-ENGINE-IX).

## Contract

Validate-then-allocate; `false` on bad alphabet/length/capacity with the matrix
untouched (MEL-ENGINE-VIII). Pure compute, no syscalls. Caller frees the matrix.

## Status

`matrix`, the linear family, and the 2D substrate (`bitwriter`, `galois`, `rs`)
are built and verified. Next: QR (segmentation → encode → RS interleave →
placement → masking) atop the substrate, then DataMatrix/Aztec/PDF417, then
decode. Everything still under *sequenced* is designed-for, not yet built —
`MEL-ENGINE-I`, deferral is never refusal.
