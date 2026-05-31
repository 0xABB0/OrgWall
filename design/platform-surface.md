# Melody surface — Architecture Spec (the swapchain bind-handle)

## 0. Correction
A prior draft named `Mel_Platform_Surface` "the window the swapchain binds to," with a six-state lifecycle and twelve-event taxonomy as a `platform.surface` module. Wrong: `gpu_view.h` already shows a GPU view "owns no window of its own" and `mel_gpu_view_surface` returns the *view's* `NSView*` — the swapchain attaches to a drawable region inside a window, not the window. The window/display/app events moved to `design/window.md` and `modules/display`. What remains is thin.

## 1. What it is
The surface is the bind-point a GPU presenter attaches to — what `VkSurfaceKHR`, `wgpu::Surface`, and the `IDXGISwapChain` target already are: a handle created from an OS drawable region, consumed by `Mel_Gpu_Swapchain` (`design/gpu-rhi.md` §7.4). It is a `gpu` concept; the original `Mel_Gpu_Surface` name and placement stand. It is off the render path (rendering targets an image; presentation is the optional downstream — headless GPU has no surface) and off the 2D path (the painter's borrowed window context is not a surface).

## 2. Construction — from a vended native handle
`Mel_Gpu_Surface` wraps an opaque native handle from one of four sources, indistinguishable to the swapchain (MEL-ENGINE-IX):
- a `gui` view — `mel_gpu_view_surface(h)`;
- a window's content region — `mel_window_content_native(w)`;
- an app-supplied borrowed handle — no engine window; not owned by the surface;
- none — headless; the swapchain is replaced by an owned texture.

Native types at the wrap site: Win32 child `HWND`, macOS/iOS `NSView*`/`UIView*`/`CAMetalLayer*`, Android `ANativeWindow*`, Wayland `wl_surface*`, X11 `Window`, Web canvas selector, EGL `EGLSurface`.

## 3. Signals
The swapchain needs two, both from the vendor (`design/window.md` §6): reconfigure on extent change, rebuild on backing lost/replaced. On synchronous-destruction platforms (Android `surfaceDestroyed`, Wayland disconnect, iOS VC invalidation) the bound swapchain releases before the OS callback returns — discipline owned by the GPU teardown path (`design/gpu-rhi.md`). Scale, occlusion, orientation, HDR, migration are read from `window`/`display`, not the surface.

## 4. Fullscreen
Exclusive acquire/release and `pre_rotation` stay in `gpu` (`design/gpu-rhi.md` §7.4); exclusive mode is contingent on the window owning the display, so eviction is a window-manager event reported by `window`.
