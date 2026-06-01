# paint — todo

Deferred slices, in order. Each is a mechanical lift from `gui`'s existing per-backend
painter except where a correctness gotcha is flagged — verify on the target platform.

## Backends (port the 7 ops + a pixmap ctor with readback)

- **winui** — GDI. Window `HDC` from `BeginPaint`; pixmap from `CreateCompatibleDC` +
  top-down `CreateDIBSection`. **Gotcha:** DIB is **BGRA** — swizzle to RGBA on readback so
  `mel_pixmap_pixels` returns `mel_color8` order. `static g_font` is single-thread.
- **dom** — Canvas2D. Window `<canvas>`; pixmap `OffscreenCanvas`; readback `getImageData`.
- **androidnative** — `Canvas`+`Paint`. Pixmap `Bitmap`+`new Canvas`; readback
  `copyPixelsToBuffer`. `JNIEnv` is thread-bound.
- **soft** — hand-written rasterizer + bundled bitmap font (text is the long pole).
  Deferred; first customer the Linux CLI. Portable C, verifiable on the host.

## Borrowed-window drawable + gui migration

- **Done:** the borrowed-window drawable ctor (`mel_drawable_borrow`, `owns=false`) wraps an
  external native context; `mel_drawable_release` drops the handle and bumps the slotmap
  generation but never touches the borrower's context/buffer, so a retained drawable fails
  `alive()`. Lives in `registry.c` (backend-agnostic; stores the opaque `void* native`).
  Unit-tested host-side by `test/borrow_test.c` wrapping a self-owned `CGBitmapContext`.
- **Done (cocoa only):** `gui`'s cocoa canvas `drawRect` vends a borrowed `Mel_Drawable`
  through `on_paint`; `gui` now `mel_depends` on `paint`+`color`; deleted `gui/painter.h`,
  `gui/color.h`, `gui/src/cocoa/painter.m`, and the private `struct Mel_Painter`; the three
  gui apps draw `mel_painter_*` with `mel_color8`. Verified live (`./nob run hello-world-gui
  macos` — canvas renders upright, correct colors/coords).
- **Debt — the other four backends now break on their platforms.** A unified public
  `on_paint(Mel_Painter*)` cannot be paint's type for cocoa and `gui`'s type elsewhere, so the
  switch is repo-wide: `gui/src/{winui,androidnative}/painter.*` and the inline painters in
  `gui/src/{uikit,dom}/canvas.*` still `#include <gui/painter.h>` (deleted) and reference the
  old `Mel_Color`. They will not compile under `.backend = winui|dom|androidnative|uikit` until
  `paint` grows those backends and each canvas is rewired to vend a borrowed drawable. macOS
  builds green because those sources are gated out. dom (wasm) and android are host-testable
  once their paint backends land; win32 is not testable from this host.
- The single-active-painter rule already relaxed: `begin` asserts per-drawable `!painting`,
  not a global, and `Mel_Painter` is a stack value — any number live at once.

## Cleanups

- Build axis: paint backend default-by-platform, independent of `gui`'s `.backend`.
- `Mel_Drawable`/`Mel_Pixmap` are typedef aliases — no compile-time misuse catch. Consider
  nominal one-field-struct wrappers once the borrowed path exists to confuse them.
- Logging/profiling (MEL-CODE-006): none yet; add on create/destroy + alloc-fail once the
  module grows a `log` dep.

## Future

- **Drawn backend** — record ops into a `gpu` draw-list instead of a native 2D API: the
  GPU-accelerated 2D path that rejoins the swapchain. Deferred until the GPU RHI lands.
  Composes with the existing op set (same painter surface, a different lowering).

## Notes

- The `MEL_TEST` harness works (runtime/main in `tools/test/src/runner.c`); `build.c` links
  it explicitly because `mel_add_test` does not auto-link it. If the build framework gains
  auto-linking for `is_test` targets, drop the explicit `runner.c` source from `build.c`.
- `mel_assert` is still a no-op (both branches of `debug/assert.h` empty), so paint's
  liveness/`owns`/`painting` asserts are inert; `alive()` works via slotmap generation. They
  fire once `mel_assert` gets a body.
