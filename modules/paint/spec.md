# paint — spec

Immediate-mode 2D, extracted from `gui`. Painter (transient cursor) draws into a drawable
(borrowed window context or owned `Mel_Pixmap`). Backend compile-time selected, one op set
across window and pixmap. Deps: `core`, `allocator`, `collection`, `math`, `string`,
`color`, `debug`. Not `gpu`/`platform`/`gui`.

## Objects

- `Mel_Drawable`, `Mel_Pixmap` — `Mel_SlotMap_Handle` aliases into one module-global
  drawable slotmap. `bool owns` discriminates owned-pixmap vs borrowed-window (MEL-CODE-001:
  a bool, not a kind enum). Not nominal C types; misuse caught at runtime by `owns`.
- `Mel_Painter` — opaque cursor, one active per thread (`thread_local`, no heap alloc).
- `Mel_Pixmap_Pixels` — `{ mel_color8* pixels; i32 stride; i32 w, h; }`; stride is
  bytes/row, never assume `w*4`.

## API

    Mel_Pixmap        mel_pixmap_create(const Mel_Alloc*, i32 w, i32 h);
    Mel_Drawable      mel_pixmap_drawable(Mel_Pixmap);
    Mel_Pixmap_Pixels mel_pixmap_pixels(Mel_Pixmap);
    void              mel_pixmap_destroy(Mel_Pixmap);
    bool              mel_drawable_alive(Mel_Drawable);
    Mel_Painter*      mel_painter_begin(Mel_Drawable);
    void              mel_painter_end(Mel_Painter*);
    /* clear, fill_rect, fill_ellipse, stroke_rect, draw_line, fill_round_rect, draw_text */

## Conventions

- Color args are **straight** `mel_color8`; pixmap storage is **premultiplied** RGBA8. The
  backend premultiplies on draw — never premultiply the arg too.
- Origin top-left, y-down. The pixmap flips the CTM at creation so drawing is y-down **and**
  memory is top-down (row 0 = top scanline).
- Pixmap is device pixels: `CGColorSpaceCreateDeviceRGB` (not sRGB), so fills land verbatim.
- Pixel buffer takes the caller's `Mel_Alloc` (MEL-CODE-003); the registry table takes the
  heap allocator, set in a `MEL_CONSTRUCTOR` (predates any caller allocator).
- `draw_text`: `pos` is top-left; baseline placed at `pos.y + ascent`, context y-flip
  cancelled locally so glyphs draw upright.

## Backends

- `quartz` (`src/quartz/*.c`, `MEL_ON(MACOS)|MEL_ON(IOS)`) — CoreGraphics + CoreText, pure C
  (no AppKit/UIKit, no ObjC runtime). Owned-pixmap path implemented.
- winui / dom / androidnative / soft — see `todo.md`.

Verification: `./nob run paint-example macos` (self-checks + PPM dump to `/tmp`). No unit test
yet — as of this writing the `MEL_TEST` harness (`modules/test`) has no runtime/main, so
`test/pixmap_test.c` cannot run; wire it once the harness works.
