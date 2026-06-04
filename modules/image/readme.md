# image

The CPU-side pixel buffer: a typed, allocator-backed raster any source fills
(camera, codec, painter, screen) and any consumer reads (barcode decode, GPU
upload, painter backing). See `spec.md` for the full surface and build order.

## Why

A pixel buffer is the seam every visual subsystem meets at, and re-deriving it
per module — `paint`'s premultiplied canvas, a camera's planar YUV frame, a
decoder's luminance plane — splinters the one concept into incompatible dialects.
This module owns it once: format-plural, allocator-backed, zero-copy where the
data already permits it.

## Model

`Mel_Image` is a caller-owned plain struct (`init`/`free`, like
`mel_barcode_matrix`), never a slotmap handle and never a hidden global. Pixel
format is an open descriptor (`mel_image_format`) referenced by `const*` — the
`Mel_Alloc`/galois pattern — not an enum: a format carries its own geometry and
behaviour, and a new one is a new descriptor touching nothing existing
(MEL-CODE-001). `gpu`'s format enum is the protocol exception; a CPU raster
format is the open opposite.

Storage is one contiguous, optionally row-aligned allocation for owned images;
`mel_image_wrap` borrows external per-plane memory (a camera frame) with zero
allocation and a no-op free. Planes are reached through `mel_image_plane`, so
neither path needs a fixed `planes[N]` (MEL-CODE-002).

`mel_image_gray` is the borrowed luminance view consumers decode against. For a
format whose plane 0 *is* 8-bit luminance (gray8, the Y of NV12/I420),
`mel_image_gray_borrow` returns it zero-copy; otherwise `mel_image_to_gray`
converts.

## Layout

- `format` — the open descriptor, predefined singletons (packed RGBA/BGRA/RGB/
  gray/r/rg/16f/32f, planar NV12/NV21/I420/I422/I444), geometry per family.
- `image` — `Mel_Image` lifecycle, plane resolution, byte sizing.
- `convert` — luminance views now; the full any-to-any conversion matrix
  (canonical intermediate + direct kernels) sequenced next.
- *Sequenced:* conversion matrix, geometry (resize/orient/blit), codecs (stb),
  gpu-format mapping, then the `paint` rebase onto `Mel_Image`.

## Dependencies

`core`, `allocator`, `debug`. `color` (colour spaces, luma) and `collection`
(codec registry) join as conversion and codecs land. No `gpu`, no `paint` —
`image` sits below both.

## Contract

Validate-then-allocate; `false` on bad dimensions/format/OOM with the target
untouched (MEL-ENGINE-VIII). Owned buffers free through the same allocator;
wrapped buffers are never freed. Conversion never silently approximates — an
unsupported pair fails loudly rather than emitting wrong pixels.
