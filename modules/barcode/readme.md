# barcode

Barcode symbol generation: payload bytes → a grid of dark/light modules a caller
rasterizes via `gui`/`gpu`/`color`. Linear (1D) and, sequenced, 2D and decode.
See `spec.md` for the architecture and phased build plan; the design rationale
lives in `design/barcode.md`.

## Why

Putting a scannable mark on screen, paper, or a part is a recurring need across
GUI apps, kiosks, logistics tools, and tickets — and every from-scratch encoder
re-derives the same guard patterns, check digits, and (for 2D) Reed–Solomon
algebra, usually with a subtle parity or checksum bug a scanner silently
tolerates until it doesn't. This module owns that vocabulary once.

## Model

The single boundary type is `mel_barcode_matrix`: a row-major byte grid, `1`
dark / `0` light, sized in modules, carrying its allocator and an advised
quiet-zone width. A 1D symbology is a grid of `height == 1`; a 2D symbology a
square or rectangular grid. There is no symbology enum (MEL-CODE-001): each
symbology is a distinct `mel_<sym>_encode` function, and per-family parameters
ride a small by-value options struct.

The module emits *modules*, never pixels and never color. Quiet zone is advised,
not painted — its color is the light color, which `barcode` does not know
(MEL-ENGINE-IX).

## Layout

- `matrix` — `mel_barcode_matrix`, init/free/get/set and the column helper the
  linear painters use.
- `linear` — Code 39, EAN-13/EAN-8/UPC-A, ITF, Code 128. Self-checking and
  check-digit symbologies, validated before allocation.
- *Sequenced:* `bitwriter` + a field-parameterized `galois`/Reed–Solomon coder
  (the 2D substrate), then `qr`, `datamatrix`, `aztec`, `pdf417`, then decode.

`barcode.h` is the umbrella; reach for an individual header to pull one family.

## Dependencies

`core` (types), `allocator` (every buffer is allocator-backed, MEL-CODE-003),
`collection.array` for working buffers. No `math`, no `gpu`; libc `<string.h>`.

## Contract

Encoders validate alphabet and length before any allocation and return `false`
on violation, leaving the matrix untouched — never a corrupt symbol
(MEL-ENGINE-VIII). On success the matrix owns an allocator-backed grid the caller
frees with `mel_barcode_matrix_free`. Encoding is pure compute: no syscalls, no
hidden threads, portable to every target.

## Status

Foundation (matrix + linear family) building, verified against published test
vectors. 2D substrate, 2D symbologies, and decode are sequenced per `spec.md`.
