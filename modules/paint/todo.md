# paint — todo

Deferred slices, in order. Each is a mechanical lift from `gui`'s existing per-backend
painter except where a correctness gotcha is flagged — verify on the target platform.

## Backends

The borrowed-window path (begin/end + the 7 ops) ships on every backend `gui` targets; every
gui canvas is migrated. The owned `Mel_Pixmap` path stays quartz-only (gui doesn't use it).

- **gdi** (`src/gdi/*.c`, win32) — **done** (borrowed). `HDC` native; `g_font` single-thread;
  UTF-8→UTF-16 via `MultiByteToWideChar`. Compile-verified with `zig cc -target
  x86_64-windows-gnu`; `./nob build … win32` can't link it — pre-existing nob toolchain gap
  (plain `clang` + `x86_64-windows-msvc`, no Windows SDK, triple not applied to cflags).
- **dom** (`src/dom/*.c`, wasm) — **done** (borrowed). Canvas2D via `EM_JS`; `native` is the
  canvas element id, resolved through `gui`'s `MelWeb.els`. Builds + links (`.wasm/.js/.html`).
  **Runtime coupling:** paint's web ops read `gui`'s JS element registry — the borrowed model
  leaks here because a JS canvas context can't cross the C ABI except by index into a shared JS
  table that `gui` owns. Proper fix: a shared `dom` micro-module owning the registry, depended on
  by both. Not browser-run-verified yet.
- **android** (`src/android/*.c`) — **done** (borrowed). JNI `Canvas`/`Paint`; `native` is
  `Mel_Paint_Android_Native`; method IDs cache lazily off the passed env (no `platform` dep).
  Compiles; `.so` links + **loads** on device (after the `-lm`/`-landroid` link fixes, below).
  **Not boot-verified:** the app still crashes at `MelGui.nativeRegister` — `gui`'s JNI entry
  points live in `libgui.a` and the linker GCs archive members nothing references, so they are
  absent from `libmelody.so`. Pre-existing (affects all gui android JNI, not paint; the app never
  booted on android before). Fix is `-Wl,--whole-archive` for the gui static lib in the android
  `.so` link — a build-framework change with broad blast radius (pulls every object, likely
  surfacing gpu/EGL deps); left as its own task.
- **soft** — still deferred (no gui consumer; for the Linux CLI). Rasterizer + bitmap font.

### Owned-pixmap path on non-quartz backends
Deferred. gdi (`CreateDIBSection`, BGRA→RGBA readback swizzle), dom (`OffscreenCanvas` +
`getImageData`), android (`Bitmap` + `copyPixelsToBuffer`). Implement when a non-macOS host wants
offscreen `Mel_Pixmap`; until then `mel_pixmap_create`/`destroy`/the pixmap test are quartz-only.

### Android `.so` system-lib link (fixed this slice, in `modules/build/emit.c`)
The android `.so` link linked no NDK system libs; `-lm` (color's `powf`) and `-landroid`
(reactor's `ALooper_*`, window's `ANativeWindow_*`) were added. Both were always needed — the app
never loaded on android before.

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
