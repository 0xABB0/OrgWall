# paint

Immediate-mode 2D drawing, extracted from `gui`. Two primitives: the **painter**
(transient drawing cursor) and the **drawable** (its target — a borrowed window
paint-context or an owned `Mel_Pixmap`). The backend is compile-time selected; each op
lowers to the platform's native 2D API. No `gpu`, `platform`, or `gui` dependency.

Design: `spec.md` (this module's contract); `todo.md` (deferred slices, actionable).

## State

The **quartz** backend (CoreGraphics+CoreText, pure C, no AppKit/UIKit, no ObjC runtime;
serves macOS now, iOS later) ships both drawable kinds:

- **Owned offscreen** — `Mel_Pixmap` create / drawable / pixels (CPU readback) / destroy.
- **Borrowed window** — `mel_drawable_borrow` / `mel_drawable_release` wrap an external native
  context (`owns = false`); every `gui` canvas (cocoa, uikit, gdi, dom, android) vends one through
  `on_paint`. This is the extraction made real: `gui` now depends on `paint`, all its per-backend
  painters and the private `Mel_Color` are retired, and the apps draw one `mel_painter_*` op set
  into a live window on every backend.
- `Mel_Painter` begin / end + the seven ops (`clear`, `fill_rect`, `fill_ellipse`,
  `stroke_rect`, `draw_line`, `fill_round_rect`, `draw_text`).
- Module-global drawable slotmap, eagerly initialized via `MEL_CONSTRUCTOR`.
- Color is `mel_color8` (`color` module), not a paint-private type.

## Backends

`quartz` (macos/ios), `gdi` (win32), `dom` (wasm), `android` — all ship the borrowed-window path;
`gui` is migrated on every one. quartz alone also has the owned-`Mel_Pixmap` path. `soft` (Linux
CLI rasteriser + bitmap font) stays deferred. Build/verification state per backend, the deferred
owned-pixmap path on non-quartz, the dom JS-registry coupling, and the android boot blocker are in
`todo.md`.

## Dependencies

`core`, `allocator`, `collection` (slotmap), `math`, `string`, `color`, `debug`.

## Build & verify

    ./nob build paint macos
    ./nob test  paint-pixmap macos   # unit test via the MEL_TEST harness
    ./nob run   paint-example macos   # also dumps /tmp/paint-example.ppm to eyeball
