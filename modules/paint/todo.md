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

- Add the borrowed-window drawable ctor (`owns=false`): wraps an external native context,
  `destroy` releases the handle but **not** the borrower's context/buffer.
- `gui` canvas `on_paint` vends a `Mel_Drawable` (borrowed); delete `gui/painter.h`,
  `gui/color.h`, and each `gui/src/<backend>/painter.*`; retire `gui`'s private `Mel_Color`
  for `mel_color8`. This is what makes the extraction real — and is macOS-verifiable.
- With the gui drawRect frame available, the painter can embed there; the `thread_local`
  single-active-painter rule can relax.

## Cleanups

- Build axis: paint backend default-by-platform, independent of `gui`'s `.backend`.
- `Mel_Drawable`/`Mel_Pixmap` are typedef aliases — no compile-time misuse catch. Consider
  nominal one-field-struct wrappers once the borrowed path exists to confuse them.
- Logging/profiling (MEL-CODE-006): none yet; add on create/destroy + alloc-fail once the
  module grows a `log` dep.

## Blocked on repo (not paint's to fix)

- `MEL_TEST` harness has no runtime/main → `test/pixmap_test.c` cannot run.
- `mel_assert` expands to nothing → paint's liveness/`owns`/`painting` asserts are inert;
  `alive()` still works via slotmap generation.
