# 2026-06-01 — paint: borrowed-window drawable + cocoa gui migration

## Work done

The keystone slice from `todo.md` — the one that turns `paint` from a standalone offscreen
library into `gui`'s real painter. End-to-end on the cocoa backend, verified in a live window.

- **Borrow ctor + lifetime contract** (`src/registry.c`, backend-agnostic):
  `mel_drawable_borrow(void* native, i32 w, i32 h)` inserts an `owns=false` record holding the
  opaque native context; `mel_drawable_release` removes it — bumping the slotmap generation so a
  retained `Mel_Drawable` fails `mel_drawable_alive`, and **never** touching the borrower's
  context or buffer. Decision: this lives in `registry.c`, not `quartz.c`, because it only
  stores a `void*` — it works unchanged for every future backend.
- **Host test** `test/borrow_test.c` (gated `MACOS|IOS` in `build.c`): builds its own
  `CGBitmapContext`, wraps it borrowed, draws, reads back, then asserts `release` invalidates the
  handle yet leaves the test's buffer intact (post-release sample still reads the drawn pixel).
  `./nob test paint-pixmap macos` → 3/3 green.
- **gui cocoa wiring:** `MelGuiCanvasView drawRect` now does
  `borrow(ctx) → begin → on_paint → end → release` over the AppKit `CGContextRef`. The view is
  `isFlipped=YES`, so its context is already y-down — matching the quartz op set; paint applies
  no extra flip on a borrowed drawable. CoreText `draw_text`'s local baseline flip-cancel
  restores upright glyphs in that context (the one real correctness risk; confirmed visually).
- **Retired in gui:** `include/gui/painter.h`, `include/gui/color.h`, `src/cocoa/painter.m`, and
  the private `struct Mel_Painter` in `macos.h`. `gui.h` re-exports `<paint/painter.h>` +
  `<color/rgba8.h>`; `controls/canvas.h` pulls paint's painter; `build.c` adds `paint`+`color`.
- **Apps migrated** (11 call sites, 3 apps): `mel_rgb`→`mel_color8_rgb`, `Mel_Color`→`mel_color8`;
  each app `build.c` gains explicit `color`+`paint` deps. The `on_paint(Mel_Painter*)` signature
  is textually unchanged — only the underlying struct moved to `paint`.
- **Verified live:** all three apps build, link, package on macOS; `hello-world-gui` launched and
  screenshotted — the canvas renders the dark `clear`, the bordered `stroke_rect`, the `draw_line`
  divider, and **"canvas: drag to paint" upright at top-left** in the right colour. y-down
  coordinates and CoreText orientation correct through the borrowed window context.
- Docs: `spec.md`/`readme.md`/`todo.md` updated to reflect the shipped borrowed path; fixed the
  stale `design/paint.md` pointers (that file was retired into the module last session); a note
  added to `gui/todo.org`.

## Kludges / debt (MEL-ENGINE-VIII — confess all)

- **The four non-cocoa gui backends now break on their platforms.** A unified public
  `on_paint(Mel_Painter*)` cannot be paint's type for cocoa and `gui`'s old type elsewhere, so the
  switch is repo-wide. `gui/src/{winui,androidnative}/painter.*` and the inline painters in
  `gui/src/{uikit,dom}/canvas.*` still `#include <gui/painter.h>` (deleted) and use `Mel_Color`;
  they will not compile under `.backend = winui|dom|androidnative|uikit`. This is the accepted
  cost of "end-to-end cocoa," but it is **real breakage, not "left on the old path"** — and it is
  blocked: those canvases can only migrate once `paint` grows their backends. macOS is green
  because the build gates those sources out. dom (wasm) and android are host-testable once their
  paint backends land; win32 is not testable from this host.
- **`mel_assert` remains a no-op** (per the module note; unchanged this session). So
  `borrow`'s `native && w>0 && h>0` and `release`'s `!owns`/`!painting` asserts are inert. The
  borrowed lifetime contract therefore rests entirely on the slotmap generation bump — which does
  work (the test proves `!alive` after `release`). But a retained **painter** (not drawable) past
  `end`/`release` still caches a dangling `native` and its ops would draw into a freed/re-vended
  context with no assert; only the drawable *handle* invalidates loudly. The painter-side guard
  is dormant until `mel_assert` gets a body.
- **`borrow_test` is quartz-only.** It must fabricate a real `CGBitmapContext`; there is no
  portable borrow test because the borrow path inherently needs a native context.
- **Transient self-inflicted bug, caught pre-build:** my `Mel_Color `→`mel_color8` replace
  dropped the trailing space (`mel_color8bg`); grepped `mel_color8[a-z]`, repaired all seven
  before the first compile.
- **Worktree off local HEAD.** `origin/main` is 4 commits behind local `main` (the paint WIP), so
  a default `fresh` worktree would have stranded the module. Work is on branch `paint-borrow`
  off HEAD; **not yet merged to `main`** — Gabbo to merge/cherry-pick.

## CLAUDE.md suggestions (recommendations only)

- None for CLAUDE.md. Repo items (a task each): give `mel_assert` a body — without it the
  painter-side half of this slice's lifetime contract is unenforced; and prioritise `paint`'s
  dom + android backends next, since both unblock a non-cocoa gui canvas migration **and** are
  verifiable on this host (corrected from my earlier wrong claim that only cocoa was).

## Suggestions

- Next slice with leverage **and** host verifiability: `paint` dom (wasm, `<canvas>`/
  `OffscreenCanvas`) or android (`Canvas`+`Paint`) backend, then rewire that gui canvas to vend a
  borrowed drawable — clearing the breakage above one backend at a time. winui (BGRA DIB swizzle,
  flagged in `todo.md`) and soft (rasteriser + bitmap font, the long pole) remain.
- The borrowed path now exists, so the `todo.md` cleanup is unblocked: nominal one-field-struct
  wrappers for `Mel_Drawable`/`Mel_Pixmap` to get a compile-time misuse catch (today only the
  runtime `owns` flag distinguishes them).
