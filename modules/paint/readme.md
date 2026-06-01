# paint

Immediate-mode 2D drawing, extracted from `gui`. Two primitives: the **painter**
(transient drawing cursor) and the **drawable** (its target — a borrowed window
paint-context or an owned `Mel_Pixmap`). The backend is compile-time selected; each op
lowers to the platform's native 2D API. No `gpu`, `platform`, or `gui` dependency.

Design: `design/paint.md` (full), `design/paint-quartz-pixmap.md` (slice 1).

## State

Slice 1 — the **owned offscreen** path on the **quartz** backend (CoreGraphics+CoreText,
pure C, no AppKit/UIKit, no ObjC runtime; serves macOS now, iOS later):

- `Mel_Pixmap` create / drawable / pixels (CPU readback) / destroy.
- `Mel_Painter` begin / end + the seven ops (`clear`, `fill_rect`, `fill_ellipse`,
  `stroke_rect`, `draw_line`, `fill_round_rect`, `draw_text`).
- Module-global drawable slotmap, eagerly initialized via `MEL_CONSTRUCTOR`.
- Color is `mel_color8` (`color` module), not a paint-private type.

## Deferred

winui / androidnative / dom / soft backends; the borrowed-window drawable and the `gui`
canvas migration; an alloc-free embedded painter. See `design/paint.md` §7.

## Dependencies

`core`, `allocator`, `collection` (slotmap), `math`, `string`, `color`, `debug`.

## Build

    ./nob build paint macos
    ./nob test  paint-pixmap macos
