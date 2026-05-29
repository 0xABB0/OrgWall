# Melody `platform.surface` — Architecture Spec

`platform.surface` is the engine's window abstraction — the OS-level drawable region the GPU swapchain attaches to. The surface is a *window*, not a GPU object; it outlives swapchain rebuilds, survives display migration, and reports its own lifecycle independent of any presentation engine bound to it. The renaming from the prior `Mel_Gpu_Surface` to `Mel_Platform_Surface` reflects that the underlying primitive is the platform's window handle, not a presentation-engine artifact. The GPU swapchain is the *downstream consumer* of the surface, not its owner.

This document is bound by the Ten Commandments of the Engine. Where a decision turns on one, the commandment is cited by tag (`MEL-ENGINE-N`).

---

## 1. Module placement

- Parent module: `platform` — already houses the per-OS event-loop, input, and clipboard surfaces under `modules/platform/`.
- This module: `platform.surface` — the window wrapper and its lifecycle.
- Siblings under `platform`: `display` (the `Mel_Display` that a surface lives on, formerly `Mel_Gpu_Output`), `sensor` (accelerometer, gyroscope, ambient light, compass).
- Downstream: `gpu` consumes `Mel_Platform_Surface` when constructing `Mel_Gpu_Swapchain`. XR sessions do **not** bind to `platform.surface` — OpenXR / visionOS Compositor Services own the compositor layer set directly and pipe through GPU's borrowed-image swapchains (§7.4 of `docs/gpu-rhi.md`). Flag at integration sites: a surface is not a precondition for an XR pipeline.

## 2. Inherited principles

The surface inherits the cross-cutting policies established in `docs/gpu-rhi.md`:

- **P2 — full-control escape hatches.** Every engine-managed surface convenience exposes the underlying native handle so the app can reach the OS directly when its product demands it. The simple path is the powerful path further along (MEL-ENGINE-II).
- **MEL-ENGINE-III — no stolen cycles.** Events are subscription-based, not polled. A surface that is `Backgrounded` or `Occluded` consumes nothing it was not asked to. The pacing source (`docs/frame-pacing.md`) and the surface event stream are decoupled by design.
- **MEL-ENGINE-V — respect the user's product.** The platform never silently re-scales the swapchain on a DPI / scale-factor change. Re-flow versus resize is the app's editorial decision; the engine reports the change and waits.
- **MEL-ENGINE-VIII — fail with honor.** Every state change visible to the app; no silent recreate. Surface loss, display migration, layer replacement, and canvas reconfiguration each fire a distinct event with a distinct semantic — never a single ambiguous "something changed" callback.

## 3. Public objects

- `Mel_Platform_Surface` — the value handle, wrapping `Mel_SlotMap_Handle` per the §3.1 identity model. One slotmap on the platform module; generations turn use-after-destroy into a loud `alive()` failure.
- `Mel_Platform_Surface_Descriptor` — passed at construction. Carries the platform-specific native handle in a tagged union: Win32 `HWND` + `HINSTANCE`, macOS `NSView*` or `CAMetalLayer*`, iOS `UIView*` or `CAMetalLayer*`, Android `ANativeWindow*` (plus the JNI `Surface` jobject for callback wiring), Wayland `(wl_display*, wl_surface*)`, X11 `(Display*, Window)`, Web `(canvas_selector: const char*)`, EGL `(EGLDisplay, EGLSurface)`. The engine never constructs the OS window itself when the app provides one — surfaces created from supplied native handles are `Borrowed` and not destroyed on `surface_destroy`.
- `Mel_Platform_Surface_State` ∈ `{ Alive, Occluded, Minimized, Backgrounded, Lost, Destroyed }` — the surface's lifecycle. Distinct from the swapchain's status.
- `Mel_Platform_Surface_Event` — tagged union over the taxonomy in §5.
- `Mel_Platform_Surface_Handler` — the callback shape: `void (*)(Mel_Platform_Surface, const Mel_Platform_Surface_Event*, void* user)`. Registered via `surface_subscribe(surface, mask, handler, user) → Mel_Platform_Surface_Subscription`; unsubscription returns the slot.

## 4. Surface lifecycle

The state enum is independent of the swapchain. A surface is `Alive` while it has a valid native backing and the OS will deliver it pixels; `Occluded` when another window or the compositor has covered it (Windows `WM_SIZE` with `SIZE_MINIMIZED` is *not* occlusion, it is `Minimized`); `Minimized` when the window manager has iconified it; `Backgrounded` when the OS suspends the app (iOS / Android `onPause`, Web `visibilitychange → hidden`); `Lost` when the platform invalidates the backing without destroying the window object (Android `surfaceDestroyed` before `surfaceCreated`, D3D `DXGI_ERROR_DEVICE_REMOVED`-class events that propagate to the surface, GPU device-removal, Wayland compositor reset); `Destroyed` is terminal.

State transitions are explicit and always pair with an event (§5). The engine never collapses `Backgrounded → Lost → Alive` into one callback; each transition fires its own event so the app's state machine matches the OS's (MEL-ENGINE-VIII).

## 5. Event taxonomy

Every event below is a distinct case in `Mel_Platform_Surface_Event`. The mask on `surface_subscribe` is a bitset of these cases. The lowerings are stated where the OS primitive is load-bearing.

- `resize { pixel_extent, point_extent }` — the OS-coordinate window size changed. Windows `WM_SIZE`, macOS `NSWindow.didResize` / `NSView.frameDidChange`, iOS `UIView.layoutSubviews` for view-bounds changes, Android `surfaceChanged` width/height, Wayland `xdg_toplevel.configure`, X11 `ConfigureNotify`, Web `ResizeObserver` on the canvas.
- `drawable_size_changed { pixel_extent }` — the device-pixel extent the GPU rasterizes into changed without an OS-coordinate resize (Retina scale toggle at fixed point size, `CAMetalLayer.drawableSize` change from app code, Web `devicePixelRatio` flip with unchanged CSS size). The swapchain almost always requires reconfiguration; the engine does not perform that silently.
- `scale_factor_changed { scale_factor, pixel_extent, point_extent }` — DPI / scale change. Windows `WM_DPICHANGED`, macOS `NSWindow.backingScaleFactor` change on display migration, iOS `UIView.contentScaleFactor`, Wayland `wl_surface.preferred_buffer_scale` (fractional via `wp_fractional_scale_v1`), Web `matchMedia('(resolution: ...)')` listener on `devicePixelRatio`.
- `orientation_changed { orientation }` — see §7.
- `display_migration { from: Mel_Display, to: Mel_Display }` — the window crossed a monitor boundary. Windows `WM_DISPLAYCHANGE` + per-monitor DPI events, macOS `NSWindow.didChangeScreen`, Android `Display.getDisplayId` change. The downstream swapchain may need rebuilding if the new display advertises different HDR / refresh / color-space caps.
- `hdr_capability_changed { color_spaces, peak_nits, max_fall }` — the surface's available HDR / EDR envelope shifted. macOS / iOS `NSScreen.maximumPotentialExtendedDynamicRangeColorComponentValue` updates, Windows monitor HDR toggle, Wayland color-management protocol surface-state change, Web `matchMedia('(dynamic-range: high)')`.
- `occlusion_changed { occluded: bool }` — Windows `DWM_BB_BLURBEHIND` / `IDXGISwapChain::Present` returning `DXGI_STATUS_OCCLUDED`, macOS `NSWindowOcclusionState`, Wayland `wp_presentation_feedback` discard, Web `visibilitychange` partial.
- `app_lifecycle_changed { foreground: bool }` — background / foreground transition. iOS `UIApplication.didEnterBackground` / `willEnterForeground`, Android `onPause` / `onResume`, macOS `NSApplication.didHide` / `willUnhide`, Web `visibilitychange`.
- `platform_layer_replaced { new_native_handle }` — the underlying compositor layer was swapped under the surface object. macOS `CAMetalLayer` reattachment on window mode change, Android `Surface` recreation through `SurfaceHolder.Callback`, iOS layer rehosting on view-controller transitions. The downstream swapchain must rebuild.
- `canvas_reconfigured { context_id }` — Web-specific. The `<canvas>` element's `GPUCanvasContext.configure({...})` was called externally (the app's UI framework changed format, alpha mode, or color space). The browser invalidated the current configuration.
- `surface_lost { reason: Mel_Platform_Surface_Loss_Reason }` — the native backing is no longer valid. Reasons: `android_surface_destroyed`, `wayland_compositor_reset`, `web_context_lost`, `gpu_device_removed`, `display_disconnected`, `permissions_revoked`. The swapchain bound to this surface must be released *synchronously* on certain platforms (§8).
- `fullscreen_evicted { previous_mode }` — see §8 below. Surface-level event because the eviction is a window-manager decision, not a swapchain decision. The swapchain's `fullscreen_mode` enum and its acquire / release entry points remain in §7.4 of `docs/gpu-rhi.md`; this event is the eviction notification that flows back through the surface.

Subscription is the only interface. The engine does not expose a `surface_poll_event()` because that would invert the cost — every frame paying for a queue check whether or not the app cares (MEL-ENGINE-III).

## 6. Extent semantics

Three extents travel with every surface, every event payload, and every swapchain query, and they are **never** conflated by the engine.

- `pixel_extent: { width, height }` — the device-pixel extent the GPU rasterizes into. The unit `Mel_Gpu_Swapchain` images are sized in; the unit `swapchain_acquire` returns. On Retina at 2.0, a 800×600 point window has a 1600×1200 `pixel_extent`.
- `point_extent: { width, height }` — the OS-coordinate window size. `NSView.bounds` in points on macOS; the `CALayer` size before `contentsScale` on iOS; the DPI-adjusted client area on Windows (per-monitor V2); the CSS-pixel canvas size on Web; the `wl_surface` configure size pre-scale on Wayland. This is the unit the app's UI layout system reasons in.
- `scale_factor: f32` — the ratio `pixel_extent / point_extent`. Retina ⇒ 2.0; non-Retina ⇒ 1.0; Wayland HiDPI fractional scaling ⇒ fractional (1.25, 1.5, 1.75); Web ⇒ `window.devicePixelRatio`; Windows ⇒ system or per-monitor DPI divided by 96.

The `scale_factor_changed` event fires when *any* of the three changes — a window dragged between Retina and non-Retina displays, a Wayland output scale change, a Web `devicePixelRatio` flip on browser zoom or display-config change. The platform **never** silently re-scales the swapchain; the app decides whether to resize the swapchain (preserve point-size, gain pixels) or re-flow its UI (preserve pixels, change layout) (MEL-ENGINE-V). The swapchain is a downstream consumer of the surface's extents, not an authority over them.

## 7. Orientation

Mobile surfaces rotate. `Mel_Platform_Surface_Orientation ∈ { Identity | Rotate90 | Rotate180 | Rotate270 | HorizontalFlip | VerticalFlip | HorizontalFlipRotate90 | HorizontalFlipRotate270 }` is the surface's reported orientation relative to its display's native one. The current value is queryable via `surface_orientation(surface) → orientation` and re-fired on every `orientation_changed` event. The enum is exhaustive of the eight `currentTransform` cases Vulkan reports plus the `DXGI_MODE_ROTATION_*` and `CAMetalLayer.transform` equivalents.

The orientation is a *property of the window*, observed from the window. The downstream swapchain's `pre_rotation` field (§7.4 of `docs/gpu-rhi.md`) and the projection-matrix consumption stay in GPU because they govern *what the renderer does about* the rotation. The split: this module reports the rotation; GPU consumes it.

## 8. `fullscreen_evicted` event

When a swapchain in `Exclusive` or `ExclusiveAllowTearing` fullscreen loses focus (D3D12 `DXGI_STATUS_OCCLUDED` plus minimization; Vulkan `VK_EXT_full_screen_exclusive_*` lost token; macOS exclusive-app fullscreen interrupted by Mission Control or Cmd-Tab), the *window* is what the OS evicted — the swapchain's exclusive mode is contingent on the window owning the display. The event therefore lives at the surface layer. Payload: `previous_mode ∈ { Exclusive | ExclusiveAllowTearing }`. The app may re-acquire fullscreen when focus returns by calling the swapchain's `acquire_fullscreen()` from §7.4 of `docs/gpu-rhi.md`; the engine does not silently degrade. The swapchain's `fullscreen_mode` enum and its `acquire_fullscreen()` / `release_fullscreen()` entry points remain in GPU; only the eviction notification is extracted here.

## 9. Android UI-thread synchronous release

`Mel_Platform_Surface` event handlers run **synchronously on the platform's UI thread**. On Android specifically, `SurfaceHolder.Callback.surfaceDestroyed` requires the engine to release the GPU swapchain *before the callback returns* — the UI thread will not unblock otherwise, and the next `surfaceCreated` arrives with the prior swapchain still bound to a dead `ANativeWindow`, which is silent corruption (MEL-ENGINE-VIII).

The discipline is pinned at this layer because the contract is the platform's, not the GPU's:

- The handler for `surface_lost { reason: android_surface_destroyed }` runs on the JNI-attached UI thread.
- The downstream GPU `swapchain_release` path is callable synchronously from inside the handler.
- In-flight submission completion futures (§3.3 of `docs/gpu-rhi.md`) **detach** from the surface lifetime: each future resolves with `surface_lost` status when the submission finishes draining on the GPU's reactor pump, *after* the handler has returned. The handler does not block on GPU work.
- The reactor's completion pump and the surface event delivery thread are decoupled for exactly this reason.

The same discipline holds on every platform that delivers destruction synchronously (Wayland compositor disconnect, iOS view-controller invalidation), and the engine documents the synchronous-handler contract uniformly so app code does not branch on platform inside the handler body.

## 10. Per-platform lowerings

- **Win32.** Native handle `HWND` + `HINSTANCE`. Events sourced from the `WndProc` the platform module owns (and which the app can also chain into via P2). `pixel_extent` from `GetClientRect` × per-monitor DPI; `point_extent` from `GetClientRect` directly under per-monitor V2 awareness; `scale_factor` from `GetDpiForWindow / 96`.
- **macOS.** Native handle `NSView*` or `CAMetalLayer*`. `point_extent` from `NSView.bounds`; `pixel_extent` = `point_extent` × `backingScaleFactor`; `scale_factor` from `NSWindow.backingScaleFactor`. Events from `NSWindowDelegate` + KVO on `backingScaleFactor` and `screen`.
- **iOS.** Native handle `UIView*` or `CAMetalLayer*`. Drawable size from `CAMetalLayer.drawableSize`; orientation from `UIDevice.orientation` and `UIWindowScene.interfaceOrientation`. Background / foreground from `UIApplicationDelegate`.
- **Android.** Native handle `ANativeWindow*` plus the JNI `Surface` jobject for callback registration through `SurfaceHolder.Callback`. Synchronous-release discipline as §9. Orientation through `Configuration.orientation` and `WindowManager.defaultDisplay.rotation`.
- **Wayland.** Native handle `(wl_display*, wl_surface*)`. `xdg_toplevel.configure` for resize; `wp_fractional_scale_v1` and `wl_surface.preferred_buffer_scale` for scale; the color-management protocol (stabilizing 2025-2026) for `hdr_capability_changed`. Compositor disconnect ⇒ `surface_lost { wayland_compositor_reset }`.
- **X11.** Native handle `(Display*, Window)`. `ConfigureNotify` for resize; `XRandR` for display migration and DPI. The legacy path; no HDR / fractional-scale guarantees.
- **Web.** Native handle is the canvas selector string (the engine looks it up via `document.querySelector`). Events from `ResizeObserver`, `visibilitychange`, `matchMedia` listeners on `devicePixelRatio` and `(dynamic-range: high)`. `canvas_reconfigured` fires when the app's UI framework calls `GPUCanvasContext.configure` directly.
- **EGL.** Native handle `(EGLDisplay, EGLSurface)`. Limited event surface — most events route through the host platform module (Android, embedded Linux). Carried for off-mainline embedded targets.

## 11. Sibling-module dependencies

- **Upstream: none.** `platform.surface` depends only on `platform` core and `lib/core` slotmap. It does not depend on `gpu`.
- **Downstream: `gpu`.** `Mel_Gpu_Swapchain` takes a `Mel_Platform_Surface` at creation; the swapchain's lifecycle follows the surface's per §7.4 of `docs/gpu-rhi.md`. Multi-swapchain per device implies multiple surfaces per device, which the platform module already supports.
- **Sibling: `display`.** `display_migration` event carries `Mel_Display` handles; the display module owns enumeration, HDR caps, refresh-rate envelope, and color-gamut metadata for each output. `Mel_Display` is the renaming of `Mel_Gpu_Output` from the prior spec, extracted to its own module.
- **Sibling: `sensor`.** Orthogonal — sensors do not depend on a surface and a surface does not depend on sensors. Some game UIs cross-reference both (orientation-locked content + accelerometer-driven parallax).
- **Frame pacing (`docs/frame-pacing.md`).** The pacing source is per-swapchain, not per-surface. Surface events feed the pacing source indirectly through the swapchain rebuild path (a `Backgrounded` surface lets the pacing source drop to `OnDemand`).
- **XR (`docs/xr.md`).** XR sessions do not bind to `Mel_Platform_Surface`. OpenXR's `XrSession` and visionOS's `cp_layer_renderer` own the compositor layer set directly and provide image sets to the GPU through borrowed-image swapchains. A Melody XR app may still own a `Mel_Platform_Surface` for a 2D companion window (mirror view, debug HUD), but the head-mounted display is not a surface in this module's sense.

## 12. P2 escape — native handle access

`surface_native_handle(surface) → Mel_Platform_Surface_Native` returns the platform-tagged native handle. The app may call platform APIs the engine does not wrap (custom `WndProc` extensions, AppKit window-level effects, Android `View` attachment trees, Wayland protocol extensions the engine does not yet consume). The engine continues to deliver events on its event-loop pump; the app's direct calls into the OS coexist with the engine's view of the surface. The hatch is the peer of the convenience, not a degraded subset (MEL-ENGINE-IV, MEL-ENGINE-II).

The hatch is honest about its cost. A direct OS call that mutates state the surface tracks — e.g., changing the window's bounds via `SetWindowPos` from the app — produces the corresponding event through the engine's normal channel (the OS notifies both observers). State the engine *caches* (cached scale factor, last reported extent) is refreshed on the next event; the app may also call `surface_refresh(surface)` to force a re-query of the OS-truth.
