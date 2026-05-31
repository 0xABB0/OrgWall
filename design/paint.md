# Melody `paint` — Architecture Spec

`paint` is the immediate-mode 2D drawing module, extracted from `gui`. Two primitives: the **painter** (transient drawing cursor) and the **drawable** (its target). A drawable is either a *borrowed* window paint-context or an *owned* `Mel_Pixmap`. The backend is compile-time selected; each op lowers to the platform's native 2D API, and because every such API ships an offscreen bitmap-context (`CGBitmapContextCreate`, DIB memory-`HDC`, `Bitmap`+`Canvas`, `OffscreenCanvas`), one set of ops serves both window and pixmap — the offscreen render is pixel-identical to the on-screen one. A portable software backend exists only where there is no native 2D API.

## 1. Not the swapchain, not the window

A render target is the image a producer draws into; a *window* (`design/window.md`) and a *surface* (the swapchain bind-handle, `design/platform-surface.md`) are presentation-side scaffolding the painter never touches, and neither is on the render path. Three orthogonal lineages; this module spans the latter two:

- GPU presented: a window's content region → `Mel_Gpu_Surface` → `Mel_Gpu_Swapchain` → present.
- Window 2D: a widget's transient OS 2D context from a paint callback (the `drawRect:`/`BeginPaint` callback target) → painter (exists in `gui`). This is *not* a surface and has no swapchain.
- Offscreen 2D: an owned `Mel_Pixmap` → painter → readback (new; the CLI / headless / golden-image path), with no window and no surface.

The drawable is the 2D twin of the GPU render target: both producers (painter, GPU encoder) write into either an owned target (pixmap / offscreen texture — headless) or a presented one. A future *drawn* backend recording the ops into a `gpu` draw-list is the GPU-accelerated 2D path that rejoins the swapchain — named, deferred.

## 2. Placement & dependencies

`modules/paint/`, standalone. Upstream: `core`, `allocator`, `collection` (slotmap), `math`, `string`, `color`. Not `gpu`, `platform`, or `gui`. Downstream: `gui`, and headless apps. Per-backend src mirrors `gui`: `src/cocoa`, `src/winui`, `src/androidnative`, `src/dom`, `src/soft`; one compiles per build, no vtable.

## 3. Objects

- `Mel_Drawable` — the painter's target. The backend record holds the native context plus a `bool owns` (borrowed window vs owned pixmap); no kind enum (MEL-CODE-001), backend fixed at compile time. Slotmap handle.
- `Mel_Pixmap` — owned offscreen drawable; pixel store of `mel_color8` (§6). `mel_pixmap_create(Mel_Allocator, i32 w, i32 h)`, `_drawable`, `_pixels → {mel_color8*, i32 stride, w, h}`, `_destroy`. Owns its allocator (MEL-CODE-003).
- `Mel_Painter` — drawing cursor for one pass. `mel_painter_begin(Mel_Drawable)` / `_end`; the seven ops unchanged: `clear`, `fill_rect`, `fill_ellipse`, `stroke_rect`, `draw_line`, `fill_round_rect`, `draw_text`.
- Format: RGBA8, premultiplied. Further formats as typed constructors, never an enum.

## 4. Lifetime

Owned pixmap is app-controlled; resize is destroy + recreate (no fixed buffer). A borrowed window drawable is valid only inside the paint callback that vended it — on return `gui` bumps the slotmap generation, so a retained drawable or painter fails `alive()` loudly. A painter never outlives its `begin`/`end`.

## 5. Backends

The window-binding lives only in context acquisition; the ops are written against the context handle and shared across both paths, except:

- **cocoa** — CoreGraphics + CoreText, *not* AppKit. Window context from `drawRect:`; pixmap from `CGBitmapContextCreate` (premultiplied, y-flipped to match the y-down window). `draw_text` moves off AppKit's `NSString drawAtPoint:` — which needs an implicit current context and drags in AppKit — to CoreText `CTLineDraw` on `p->cg`, so a macOS CLI links no windowing framework.
- **winui** — GDI. Window `HDC` from `BeginPaint`; pixmap from `CreateCompatibleDC` + top-down `CreateDIBSection` (readback-ready, no flip). Ops including `TextOutW` unchanged; the static `g_font` cache fixes a single-thread contract.
- **androidnative** — `Canvas` + `Paint`. Window from `View.onDraw`; pixmap from `Bitmap` + `new Canvas`, readback via `copyPixelsToBuffer`. `JNIEnv` is thread-bound.
- **dom** — Canvas2D. Window `<canvas>`; pixmap `OffscreenCanvas` (detached `<canvas>` fallback), readback `getImageData`.
- **soft** — the only hand-written rasterizer, for platforms with no native 2D API (Linux without Cairo, headless wasm). `draw_text` is the long pole: a bundled bitmap font at the floor with the limitation logged, a TTF rasterizer later. Optional, deferred; first customer the Linux CLI.

## 6. Conventions

A pixmap is device pixels only — no point-extent, no scale-factor (unlike a windowed target, `design/window.md` §5). Origin top-left, y-down; y-up backends flip at context creation. Alpha is premultiplied; `_pixels` exposes stride — never assume `w*4`.

**Open** — the painter currently takes `gui`'s private `Mel_Color`; the canonical type is `color`'s `mel_color8` (identical, lowercase). Extraction should adopt the canonical one, which forces a `Mel_*`-vs-`mel_*` house-style call. Yours.

## 7. Split (no-prerequisite first)

1. `paint-core` — the contract above. First.
2. `paint-cocoa` — CoreGraphics + CoreText (the macOS-CLI customer).
3. `paint-winui` — GDI.
4. `paint-androidnative`, `paint-dom`.
5. `paint-soft` — rasterizer + font. Deferred.
6. `gui-migration` — canvas vends `Mel_Drawable`; retire `gui`'s `Mel_Color`.
7. build axis — paint backend default-by-platform, independent of `gui`.
