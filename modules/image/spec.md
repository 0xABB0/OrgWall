# image — specification

The CPU-side pixel buffer: a typed, allocator-backed raster that any source
fills (camera, codec, painter, screen) and any consumer reads (barcode decode,
GPU upload, painter backing). This document is the contract and the map of built
versus sequenced. Rationale and failure-mode walk live in `design/image.md`.

## Principle — format is open data, not an enum (MEL-CODE-001)

A pixel format is not a closed protocol; it grows with every source and carries
behaviour (how to read a sample, how to reach luminance, how a plane is laid
out). So `Mel_Image_Format` is an open descriptor referenced by `const*` — the
`Mel_Alloc`/galois-field pattern — never a tag switched on a central table.
Adding a format adds a descriptor (a translation unit) and touches nothing
existing. `gpu`'s `Mel_Gpu_Format` *is* an enum, and correctly so: it maps 1:1
onto the Vulkan/Metal/WebGPU protocol. A CPU raster format is the opposite kind
of thing.

## Boundary types

```
typedef struct mel_image_format mel_image_format;   // open descriptor, const singletons

typedef struct {                 // borrowed, mutable view of one plane
    u8* pixels;
    i32 stride;                  // bytes between rows
    i32 w, h;                    // samples in this plane (chroma planes are subsampled)
} Mel_Image_Plane;

typedef struct {                 // borrowed, read-only luminance view — the decode contract
    const u8* pixels;
    i32       stride;
    i32       w, h;
} mel_image_gray;

typedef struct {
    const mel_image_format* format;
    i32                     w, h;     // logical image size in pixels
    const Mel_Alloc*        alloc;    // NULL ⇒ non-owning (wrapped external memory)
} Mel_Image;
```

`Mel_Image` is a caller-owned plain struct with `init`/`free`, like
`mel_barcode_matrix` — not a slotmap handle. No hidden registry, no global
(MEL-ENGINE-III). Plane storage is reached through `mel_image_plane`, never an
exposed array, so owned (single contiguous allocation) and wrapped (independent
per-plane base pointers, as a camera frame delivers) share one surface and
neither needs a fixed `planes[N]` (MEL-CODE-002).

## The format descriptor

The descriptor is geometry data plus behaviour function pointers; no numeric or
colour-space *tag* is ever switched on:

- geometry: plane count, per-plane channel count, bytes-per-sample-group,
  chroma subsampling shift (x,y). Packed and planar families share generic
  layout functions parameterised by these scalars.
- sample codec: `load`/`store` as f32 (shared `unorm8`/`unorm16`/`f16`/`f32`
  implementations assigned per descriptor).
- colour: a `const mel_color_space*` (from `color`) and an alpha mode
  (none/straight/premultiplied). YUV descriptors additionally carry range
  (full/video) and matrix (BT.601/709/2020).
- canonical transforms: `to_canonical`/`from_canonical` against the conversion
  intermediate (below).

Predefined singletons:

- packed: `mel_image_rgba8`, `mel_image_rgba8_srgb`, `mel_image_rgba8_premul`,
  `mel_image_bgra8`, `mel_image_rgb8`, `mel_image_gray8`, `mel_image_gray16`,
  `mel_image_r8`, `mel_image_rg8`, `mel_image_rgba16f`, `mel_image_rgba32f`.
- planar YUV: `mel_image_nv12`, `mel_image_nv12_full`, `mel_image_nv21`,
  `mel_image_i420`, `mel_image_i422`, `mel_image_i444`.

`mel_image_format(...)` composes a custom descriptor for the case the set did
not foresee (MEL-ENGINE-IV).

## Lifecycle

```
bool mel_image_init(Mel_Image* out, const mel_image_format* fmt, i32 w, i32 h, const Mel_Alloc* a);
bool mel_image_init_aligned(Mel_Image* out, const mel_image_format* fmt, i32 w, i32 h, u32 row_align, const Mel_Alloc* a);
bool mel_image_wrap(Mel_Image* out, const mel_image_format* fmt, i32 w, i32 h, const Mel_Image_Plane* planes, i32 count);
bool mel_image_wrap_plane(Mel_Image* out, const mel_image_format* fmt, const Mel_Image_Plane* plane);
void mel_image_free(Mel_Image* img);

i32             mel_image_plane_count(const Mel_Image* img);
Mel_Image_Plane mel_image_plane(const Mel_Image* img, i32 plane);
usize           mel_image_byte_size(const mel_image_format* fmt, i32 w, i32 h, u32 row_align);
```

Validate-then-allocate; `false` on bad dimensions/format/OOM with `out`
untouched (MEL-ENGINE-VIII). `init` allocates one contiguous, row-aligned
(`row_align`, default 1) buffer and computes plane sub-views. `wrap` is
non-owning (`free` is a no-op when `alloc == NULL`) and lets a reused frame
image rewrite its planes per frame with zero allocation. `wrap_plane` re-wraps a
single packed plane (a `plane_roi` ROI) into a `Mel_Image` so a zero-copy view
feeds `blit`/`resize`/`orient` directly (MEL-ENGINE-IX); the plane must outlive
the image, as with `wrap`. `free` zeroes the struct.

## Conversion — any format to any, correctly

```
bool           mel_image_convert(const Mel_Image* src, Mel_Image* dst);          // dst pre-inited with target format
bool           mel_image_convert_new(const Mel_Image* src, const mel_image_format* fmt, const Mel_Alloc* a, Mel_Image* out);
mel_image_gray mel_image_gray_borrow(const Mel_Image* img);                       // zero-copy; asserts a direct luma plane
bool           mel_image_to_gray(const Mel_Image* src, const Mel_Alloc* a, Mel_Image* out_gray8);
```

One general path keeps correctness: every format declares `to_canonical` and
`from_canonical` against a single intermediate — linear, premultiplied RGBA f32
— so an unforeseen pair still converts. A registry of direct kernels keeps the
hot pairs fast (NV12→gray8, RGBA8→gray8 at per-space luma weights, BGRA↔RGBA
swizzle, premultiply/unpremultiply, sRGB↔linear u8); a present kernel wins, else
src→canonical→dst. `to_gray`/`to_rgba`/premultiply are convert in disguise.
`gray_borrow` is the planar-Y fast path decode rides — the Y plane *is*
luminance, no kernel, no copy.

## Geometry

```
typedef struct mel_image_filter mel_image_filter;   // open descriptor, const singletons

Mel_Image_Plane mel_image_plane_roi(Mel_Image_Plane p, i32 x, i32 y, i32 w, i32 h);
mel_image_gray  mel_image_gray_roi(mel_image_gray v, i32 x, i32 y, i32 w, i32 h);
bool            mel_image_blit(Mel_Image* dst, i32 dx, i32 dy, const Mel_Image* src, i32 sx, i32 sy, i32 w, i32 h);
bool            mel_image_resize(const Mel_Image* src, Mel_Image* dst, const mel_image_filter* filter);
bool            mel_image_resize_new(const Mel_Image* src, i32 w, i32 h, const mel_image_filter* filter, const Mel_Alloc* a, Mel_Image* out);
bool            mel_image_orient(const Mel_Image* src, const Mel_Alloc* a, Mel_Image_Orient o, Mel_Image* out);
```

ROI is zero-copy. `blit` converts across formats. `resize` filters
nearest/bilinear/box (decode downscales for speed); the filter is an open
descriptor referenced by `const*` (`mel_image_filter_nearest`/`_bilinear`/`_box`),
the same open-data form as `mel_image_format`, never an enum. `resize_new`
allocates the destination at the target size. `orient` realises the eight
dihedral orientations as `{ i32 quarter_turns; bool flip_x; }` data: `flip_x`
mirrors in source space first, then `quarter_turns` rotates, onto which EXIF
orientation maps — a camera hands decode a correctly-rotated frame here, not in
camera or decode (MEL-ENGINE-IX).

## Codecs — read and write

```
bool mel_image_load(Mel_Image* out, const u8* bytes, usize len, const Mel_Alloc* a);
bool mel_image_load_file(Mel_Image* out, const char* path, const Mel_Alloc* a);
bool mel_image_save(const Mel_Image* img, const char* path, Mel_Image_Codec codec, const Mel_Alloc* a);
void mel_image_codec_register(const Mel_Image_Codec_Desc* codec);
```

A codec is open data — `probe`/`decode`/`encode` function pointers — so a format
is added or an implementation swapped (stb-JPEG → libjpeg-turbo) without
touching callers (MEL-ENGINE-VII/IX). The bundled codecs wrap vendored
`stb_image`/`stb_image_write`, routed through the caller allocator (STB malloc
hooks bound to a scoped current-allocator, result copied into an allocator-backed
`Mel_Image`). Read PNG/JPEG/BMP/GIF/TGA/PSD/HDR; write PNG/JPEG/BMP.

## GPU bridge

```
Mel_Gpu_Format          mel_image_to_gpu_format(const mel_image_format* fmt);
const mel_image_format* mel_image_from_gpu_format(Mel_Gpu_Format fmt);
```

Pure data mapping in `image`; `image` takes no `gpu` dependency. Upload
orchestration stays where `gpu` is already linked.

## Dependencies

`core`, `allocator`, `collection` (codec registry; reusable plane storage),
`color` (colour spaces, transfer functions, luma). `stb` vendored under
`third-party`. No `gpu`, no `paint` — `image` sits below both.

## Contract

Validate-then-allocate, `false` on violation with the target untouched
(MEL-ENGINE-VIII). Owned buffers free through the same allocator; wrapped buffers
are never freed. Conversion never silently approximates: an unsupported pair
fails loudly rather than emitting wrong pixels.

## Paint rebase

`paint` is rebased onto `image`: `Paint_Drawable` holds a `Mel_Image` (its CPU
pixels) and a native 2D context wrapping that buffer; `mel_pixmap_create`
becomes `mel_image_init(rgba8_premul)` plus a context wrap; `mel_pixmap_pixels`
returns a view of the image. One pixel-buffer concept in the tree
(MEL-ENGINE-IX, MEL-CODE-005).

## Status

Sequenced, built in order, each landing complete and tested, none stubbed:
substrate (image/format/plane/ROI) → conversion (canonical + direct kernels) →
geometry (resize/orient) → codecs (stb, registry) → gpu mapping → paint rebase.
</content>
</invoke>
