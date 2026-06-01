# paint — spec

Immediate-mode 2D, extracted from `gui`. Painter (transient cursor) draws into a drawable
(borrowed window context or owned `Mel_Pixmap`). Backend compile-time selected, one op set
across window and pixmap. Deps: `core`, `allocator`, `collection`, `math`, `string`,
`color`, `debug`. Not `gpu`/`platform`/`gui`.

## Objects

- `Mel_Drawable`, `Mel_Pixmap` — `Mel_SlotMap_Handle` aliases into one module-global
  drawable slotmap. `bool owns` discriminates owned-pixmap vs borrowed-window (MEL-CODE-001:
  a bool, not a kind enum). Not nominal C types; misuse caught at runtime by `owns`.
- `Mel_Painter` — a caller-held value (stack, no global, no heap); any number may be live at
  once. `begin` returns it; ops and `end` take `&p`. `native` is the backend context, opaque.
- `Mel_Pixmap_Pixels` — `{ mel_color8* pixels; i32 stride; i32 w, h; }`; stride is
  bytes/row, never assume `w*4`.

## API

    Mel_Pixmap        mel_pixmap_create(const Mel_Alloc*, i32 w, i32 h);
    Mel_Drawable      mel_pixmap_drawable(Mel_Pixmap);
    Mel_Pixmap_Pixels mel_pixmap_pixels(Mel_Pixmap);
    void              mel_pixmap_destroy(Mel_Pixmap);
    bool              mel_drawable_alive(Mel_Drawable);
    Mel_Drawable      mel_drawable_borrow(void* native, i32 w, i32 h);
    void              mel_drawable_release(Mel_Drawable);
    Mel_Painter       mel_painter_begin(Mel_Drawable);
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
  cancelled locally so glyphs draw upright. The local cancel restores upright glyphs in any
  y-down context — owned pixmap or borrowed window alike.
- Borrowed drawable: `mel_drawable_borrow(native, w, h)` wraps an external native 2D context
  (`owns = false`); `mel_drawable_release` drops the handle and bumps the slotmap generation
  but never touches the borrower's context or buffer. Valid only inside the paint callback
  that vended it; a retained handle then fails `mel_drawable_alive`. The vendor owns the
  context's CTM/orientation — paint applies no flip on a borrowed drawable (the cocoa canvas
  view is `isFlipped`, so its context is already y-down to match the op set).

## Backends

- `quartz` (`src/quartz/*.c`, `MEL_ON(MACOS)|MEL_ON(IOS)`) — CoreGraphics + CoreText, pure C
  (no AppKit/UIKit, no ObjC runtime). Owned-pixmap **and** borrowed-window paths; `native` is a
  `CGContextRef`. Serves both the `gui` cocoa and uikit canvases.
- `gdi` (`src/gdi/*.c`, `MEL_ON(WIN32)`) — Win32 GDI; `native` is an `HDC`.
- `dom` (`src/dom/*.c`, `MEL_ON(WASM)`) — Canvas2D via `EM_JS`; `native` is the canvas element id.
  Resolves the element through the `gui` web backend's JS registry (`MelWeb.els`) — a runtime
  coupling, see `todo.md`.
- `android` (`src/android/*.c`, `MEL_ON(ANDROID)`) — `android.graphics.Canvas`/`Paint` via JNI;
  `native` is `Mel_Paint_Android_Native` (`<paint/native_android.h>`: env + canvas + paint, vended
  per paint). Method IDs cache lazily off the passed env, so no `platform` dep.
- `soft` — deferred (no gui consumer; for the Linux CLI). See `todo.md`.

gdi/dom/android implement only the borrowed-window path (begin/end + the 7 ops) — `gui` is the
sole consumer and needs nothing else. The owned `Mel_Pixmap` path is quartz-only.

Verification: `./nob test paint-pixmap macos` runs `test/pixmap_test.c` through the `MEL_TEST`
harness (runtime: `tools/test/src/runner.c`, linked explicitly by `build.c` — see its note).
`./nob run paint-example macos` additionally dumps `/tmp/paint-example.ppm` to eyeball.
