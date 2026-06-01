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

## Addendum — the remaining four backends (same session)

On Gabbo's instruction not to leave the other backends broken, I implemented them all.

- **uikit** reuses the quartz backend (CoreGraphics under UIKit, y-down by default) — only the
  `drawRect` rewire + struct removal. `./nob build gui ios` green (ios-sim).
- **gdi** (`src/gdi/gdi.c`, win32) — ported from gui's `winui/painter.c`; `HDC` native; own
  UTF-8→UTF-16. Compile-verified with `zig cc -target x86_64-windows-gnu` (clean `.o`). nob's own
  win32 path can't build it: a **pre-existing** toolchain gap — `tc.cc="clang"`, triple
  `x86_64-windows-msvc`, no Windows SDK, triple not even applied to cflags; gui's own untouched
  winui sources fail identically on `windows.h`. Not mine to fix here.
- **dom** (`src/dom/dom.c`, wasm) — Canvas2D via `EM_JS`. `./nob build hello-world-gui wasm`
  builds + links `.wasm/.js/.html`. Runtime coupling confessed below.
- **android** (`src/android/android.c`) — JNI `Canvas`/`Paint`; new public
  `<paint/native_android.h>` carries `{env,canvas,paint}` as the borrowed `native` (the one place
  a single `void*` needed a struct); method IDs cache lazily off the passed env so paint keeps no
  `platform` dep. APK builds; `.so` links + loads on a real Pixel (`adb`).

Decisions: borrowed-only on gdi/dom/android (begin/end + 7 ops; the owned-`Mel_Pixmap` path is
quartz-only since gui never uses it); `mel_paint__drawables` made `static` (it's only touched in
`registry.c`) to avoid an extern-data relocation an android `.so` rejects — matching how the repo
avoids `-fPIC` (no module sets it).

### Kludges / debt (this wave)

- **Android does not boot yet.** Chasing the device crash uncovered a stack of **pre-existing**
  android-link gaps, all masked because the app had never actually loaded on android:
  1. `.so` linked **no** NDK system libs. `color`'s `powf` (my new gui→color dep) needed `-lm`;
     `reactor`/`window`'s `ALooper_*`/`ANativeWindow_*` needed `-landroid`. Both added to the
     android `.so` link in `modules/build/emit.c` (mirrors the existing win32 system-libs block).
     `-lm` is my regression to fix; `-landroid` was always missing.
  2. After it loads, it crashes at `MelGui.nativeRegister`: gui's `Java_*` JNI entry points live
     in `libgui.a` and the linker GCs archive members nothing references, so they never enter
     `libmelody.so`. Pre-existing, affects all gui android JNI, unrelated to paint. The fix
     (`-Wl,--whole-archive` for the gui static lib) is broad-blast-radius — it pulls every object
     and would likely surface gpu/EGL deps — so I stopped and flagged it rather than hot-patch the
     android link for every target under time pressure.
  Net: the android **paint backend** is implemented and links; the **android app** is one
  pre-existing infra fix away from booting.
- **dom runtime coupling.** paint's web ops resolve the canvas via `gui`'s JS element registry
  (`MelWeb.els`). A JS canvas context can't cross the C ABI except by index into a shared JS
  table, and `gui` owns it — so the borrowed model leaks on web. Proper fix: a shared `dom`
  micro-module owning the registry, depended on by both `gui` and `paint`.
- **Build-framework edit.** I changed `modules/build/emit.c` (android `-lm`/`-landroid`). It's
  outside paint, but necessary for the android `.so` to load and analogous to the existing win32
  handling. Flagged so it can be reviewed/reverted independently of the paint work.
- **Verification asymmetry.** Only macOS is *run*-verified (live screenshot). ios/wasm are
  build+link-verified, win32 is compile-verified (zig cc), android is link+load-verified. No
  browser or simulator run was driven.

### CLAUDE.md suggestions (this wave)

- None for CLAUDE.md. Repo tasks surfaced: (a) fix the win32 nob toolchain (apply the triple to
  cflags + a Windows SDK/`zig cc` path) — it builds nothing win32 today; (b) force-retain gui's
  android JNI exports (`--whole-archive`/`-u`) so android apps boot; (c) extract a shared `dom`
  element-registry module to decouple paint-web from gui-web.

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
