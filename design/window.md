# Melody `window` — Architecture Spec

`window` is the durable top-level OS window: decorations, lifecycle, focus, residency on a `display`, and a content region pixels mount into. Extracted from `gui`, where a Frame's `native` held the `NSWindow`, so a pure-GPU or headless app needs no `gui`.

A window is not a surface and not a swapchain. One window hosts zero or more drawable regions (surfaces); the surface is the swapchain bind-handle and lives in `gpu` (`design/platform-surface.md`). The window is presentation-side only — headless rendering has no window and no surface, and a window is never a precondition for a device or swapchain (MEL-ENGINE-I).

## 1. Placement
- Top-level module `window` (`modules/window/`), headers as `<window/...>`. Upstream: `core`, `collection` (slotmap), `display`, `platform` (event loop, input, clipboard), `reactor`. Not `gpu`, not `gui`.
- Downstream: `gui` (`design/gui-window-seam.md`) and `gpu` (binds the vended handle opaquely, never linking `window`).
- Sibling `display` (`modules/display`) owns output caps — HDR/EDR, refresh/VRR, color-space, ICC, scale, residency. The window references displays by `Mel_Display` handle and never duplicates their caps.

## 2. Objects
- `Mel_Window` — value handle over `Mel_SlotMap_Handle` (identity per `design/gpu-rhi.md` §3.1); generations make use-after-destroy a loud `alive()` failure.
- `Mel_Window_Opt` — title, geometry, style (resizable, undecorated, closable as booleans, not an enum — MEL-CODE-001), min/max, start-hidden, owner. The window-shaped fields the prior `Mel_Frame_Opt` carried (`design/gui-window-seam.md` §5). A window from an app-supplied native handle is borrowed and not destroyed on `mel_window_destroy`.
- Callbacks are grouped structs, matching `gui`'s idiom (`Mel_Gui_Lifecycle_Cb`), not a tagged-union event stream — no closed event enum (MEL-CODE-001):
  - `Mel_Window_Lifecycle_Cb` — `on_resize`, `on_move`, `on_close_request` (→`bool`, false vetoes), `on_closed`, `on_focus_in`, `on_focus_out`.
  - `Mel_Window_Display_Cb` — `on_scale_changed`, `on_display_migrated` (`from`/`to` `Mel_Display`), `on_hdr_changed`, `on_orientation_changed`.
  - `Mel_Window_App_Cb` — `on_foreground`, `on_background`, `on_occluded`.
  - `Mel_Window_Input_Cb` — raw `on_pointer_*`/`on_key_*` for the `gui`-less consumer (§7).
  - `Mel_Window_Backing_Cb` — `on_backing_lost`, `on_content_replaced` (content region's native handle swapped: macOS `CAMetalLayer` reattach, Android `Surface` recreate, iOS layer rehost). The only group a swapchain consumes (§6).

## 3. Lifecycle — orthogonal axes
Occlusion, minimization, backgrounding, and backing-validity are independent — a window can be backgrounded and occluded, minimized while foregrounded — so they are observed separately, never fused into one state value:
- **Backing validity** — `on_backing_lost` (Android `surfaceDestroyed`, Wayland reset, GPU device removal, display disconnect, web context loss, permissions revoked); regained on the next backing signal. The axis the swapchain cares about.
- **Occlusion** — `on_occluded`, distinct from minimization (`WM_SIZE`+`SIZE_MINIMIZED`).
- **App foreground/background** — `on_foreground`/`on_background` (iOS/Android pause-resume, web `visibilitychange`).

Each transition is its own signal; the engine synthesizes no compound transition (MEL-ENGINE-VIII).

## 4. Extents
Three, never conflated:
- `pixel_extent` — device pixels the content region rasterizes into; a swapchain image's size.
- `point_extent` — OS-coordinate size; the UI-layout unit (`NSView.bounds` points, Win32 per-monitor-V2 client area, CSS-pixel canvas, Wayland pre-scale configure).
- `scale_factor` — `pixel_extent / point_extent`.

`on_scale_changed` fires when any shifts; the window never silently re-scales the content region — the app decides resize-versus-reflow (MEL-ENGINE-V). An owned offscreen target carries `pixel_extent` only (`paint.md` §6).

## 5. Display residency
`on_display_migrated` carries `from`/`to` `Mel_Display` on a monitor-boundary crossing (Win32 `WM_DISPLAYCHANGE`, macOS `didChangeScreen`, Android display-id change). HDR/refresh/color-space live in `display`; `on_hdr_changed` only notifies that the envelope shifted, and the app re-queries `display`.

## 6. Content region — the surface vending point
```
mel_window_content_native(Mel_Window) -> void*   /* NSView*/HWND/ANativeWindow*/canvas host */
```
One handle, two consumers the window can't distinguish (MEL-ENGINE-IX): a GPU swapchain wraps it into `Mel_Gpu_Surface` (`design/platform-surface.md`); `gui` mounts a widget subtree into it. The swapchain needs only `Mel_Window_Backing_Cb`'s two signals — reconfigure on resize, rebuild on lost/replaced. Scale, occlusion, orientation, HDR, migration are the app's editorial concern, not the presenter's.

## 7. Window-level input
The window delivers raw window-level pointer/key (`Mel_Window_Input_Cb`) from `platform`'s event pump. `gui`, when present, layers hit-testing/focus/dispatch on top; a `gui`-less GPU app uses the raw stream. The window does no hit-testing.

## 8. Liveness
The module owns the open-window count; "last window closed ⇒ may quit" is coordinated with `app` (`mel_app_setup`, reactor owner) — relocated from `gui`, where `mel_gui__frames_dec` calls `mel_reactor_quit` today. `on_close_request` (vetoable) precedes teardown; `on_closed` fires while the handle is valid, the contract `gui`'s re-home depends on.

## 9. Per-platform lowerings
- **Win32** — `HWND`+`HINSTANCE`; events from the `platform`-owned `WndProc` (chainable, §10); extents from `GetClientRect`, scale `GetDpiForWindow/96`.
- **macOS** — `NSWindow`; content `NSView`/`CAMetalLayer`; events from `NSWindowDelegate` + KVO on `backingScaleFactor`/`screen`. The `NSApplication` bootstrap lives here (lifted from `gui`'s cocoa backend), since `window` must stand without `gui`.
- **iOS** — `UIWindow`+`UIWindowScene`; orientation from the scene, foreground from `UIApplicationDelegate`.
- **Android** — the `Activity`'s window; content `ANativeWindow*` via `SurfaceHolder.Callback`; `on_backing_lost` from `surfaceDestroyed`, synchronous on the UI thread — the bound swapchain releases before the callback returns (discipline owned by the GPU teardown path, `design/gpu-rhi.md`).
- **Wayland** — `(wl_display*, wl_surface*)`+`xdg_toplevel`; `configure` resize, `wp_fractional_scale_v1` scale; disconnect ⇒ `on_backing_lost`.
- **X11** — `(Display*, Window)`; `ConfigureNotify`, `XRandR`. Legacy; no HDR/fractional-scale guarantee.
- **Web** — document/canvas host; content `<canvas>`; events from `ResizeObserver`/`visibilitychange`/`matchMedia`; `on_content_replaced` on external `GPUCanvasContext.configure`.

## 10. Headless & native escape
A headless app creates no window; a GPU device must construct with no surface (Vulkan headless, Metal offscreen, D3D WARP, WebGPU) — the dummy-surface dance is refused (MEL-ENGINE-I). `mel_window_native(window)` returns the platform-tagged native window for OS calls the engine doesn't wrap; cached state (extent, scale, residency) refreshes on the next event, and `mel_window_refresh(window)` forces a re-query.
