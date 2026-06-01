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
  context (`owns = false`); `gui`'s cocoa canvas vends one through `on_paint`. This is the
  extraction made real: `gui` now depends on `paint`, its per-backend cocoa painter and private
  `Mel_Color` are retired, and the apps draw the same `mel_painter_*` ops into a live window.
- `Mel_Painter` begin / end + the seven ops (`clear`, `fill_rect`, `fill_ellipse`,
  `stroke_rect`, `draw_line`, `fill_round_rect`, `draw_text`).
- Module-global drawable slotmap, eagerly initialized via `MEL_CONSTRUCTOR`.
- Color is `mel_color8` (`color` module), not a paint-private type.

## Deferred

winui / androidnative / dom / soft backends; the non-cocoa `gui` canvas migration is blocked
on those backends existing (the four other backends still reference the retired `gui` painter
and will not build on their platforms until paint serves them). See `todo.md`.

## Dependencies

`core`, `allocator`, `collection` (slotmap), `math`, `string`, `color`, `debug`.

## Build & verify

    ./nob build paint macos
    ./nob test  paint-pixmap macos   # unit test via the MEL_TEST harness
    ./nob run   paint-example macos   # also dumps /tmp/paint-example.ppm to eyeball
