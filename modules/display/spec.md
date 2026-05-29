# Melody Platform — Display Enumeration (`display`)

Per-system enumeration of physical displays, their pixel geometry, refresh and VRR envelope, HDR / wide-color capabilities, color profile, and occlusion / power state. Sibling of `platform.surface` (the window-attached presentation target) and `sensor` (input devices and ambient signals); upstream of the GPU swapchain (`gpu` §7.4), which optionally targets a specific `Mel_Display` when the platform admits direct-output composition.

This module supersedes `Mel_Gpu_Output` as previously defined in `gpu-rhi.md` §5.1. Displays are a property of the *system*, not of the GPU adapter; the adapter merely reports which subset of system displays it can drive. The handle moves; the cross-reference from `Mel_Gpu_Adapter` remains.

---

## Inherited principles

- **P1 — Emulate-to-equivalent.** Backends that cannot honestly report a field omit it rather than synthesizing one. WebGPU's synthetic single-display entry (below) is the canonical case: the browser admits a containing-screen abstraction with privacy-limited fields, and `display` reports exactly that, never more.
- **P2 — Full-control escape hatch.** Every `Mel_Display` exposes its native handle (`IDXGIOutput6*`, `VkDisplayKHR`, `NSScreen*`, `UIScreen*`, JNI `Display` global ref, `wl_output*`) for apps that need integrations the engine has not foreseen.
- **MEL-ENGINE-IV.** The engine pins conventions (luminance in nits, resolution in physical pixels, refresh in millihertz) but never constrains which displays an app may target.
- **MEL-ENGINE-VI.** Battery-bearing platforms (laptops, phones, tablets) report power state alongside geometry so apps can dim, halt, or down-rez when the display is dark or backgrounded.
- **MEL-ENGINE-VIII.** Every handle carries a slotmap generation; invalidation on hot-unplug or adapter removal is a loud `alive()` failure on next use, not a dangling-pointer crash.

---

## Public objects

`Mel_Display` — typed value handle wrapping `Mel_SlotMap_Handle`. Trivially copyable, threadable, serializable. Compared by `mel_display_equal(a, b)`; identity persists across configuration changes (mode switch, HDR toggle) but rolls on hot-unplug and adapter removal. The slotmap is owned by `Mel_Platform_Instance` once that exists; until then a module-local registry owns it (`mel_display_init` / `mel_display_shutdown`).

`Mel_Display_Descriptor` — by-value snapshot returned by `display_describe(d) → { value, status }`:

- `name` — OS-published human-readable string (DDC EDID monitor name on Windows / Linux, `localizedName` on macOS, `name` on Android `Display`, `model` on Wayland `wl_output`). May be empty on backends that withhold it for privacy (Web).
- `connector` — physical connector kind where known: `Internal | HDMI | DisplayPort | USB_C | VGA | Virtual | Unknown`.
- `native_resolution` — pixel extent of the panel's native mode (`{ width_px, height_px }`).
- `physical_size_mm` — `{ width_mm, height_mm }` from EDID where published; absent on Web and on virtual displays.
- `refresh_modes[]` — list of `{ width_px, height_px, refresh_mhz, interlaced }` reported by the OS. Refresh in millihertz so 59.94 Hz NTSC and 47.952 Hz cinema cadences are exact.
- `vrr_range` — `Option<{ min_mhz, max_mhz }>`. Absent on fixed-refresh panels; present on FreeSync / G-SYNC / VESA Adaptive-Sync / HDMI 2.1 VRR / ProMotion.
- `hdr` — see "HDR and wide-color capabilities" below.
- `icc_profile` — `Option<Mel_Color_Icc_Profile>`; populated where the OS publishes a per-output profile (Windows `IDXGIOutput6::GetDesc1` color metadata + `EnumDisplayMonitors` ICC lookup, macOS `colorSpace`, Wayland color-management protocol surface). Absent on Android and Web.
- `state` — `Active | Mirrored | Disconnected | PoweredOff | Dimmed | Idle` (the OS's published power / occlusion state).
- `position_virtual` — `{ x, y }` in the OS's virtual desktop coordinate space; the anchor used to locate the display in multi-monitor layouts. Absent on Web.
- `scale_factor` — OS scale (Retina 2.0, Wayland fractional, Windows DPI ratio, Android density bucket). Reported here for layout purposes; the per-surface scale lives on `platform.surface` (`surface.scale_factor`) and is what the swapchain consumes.
- `native_handle` — opaque pointer to the platform's display object (the P2 escape).

The shared color-space enumeration `Mel_Color_Space ∈ { sRGB, Display_P3, Rec_709, Rec_2020, scRGB_linear, HDR10_PQ, HLG }` lives here and is consumed identically by GPU swapchain configuration (§7.4 `swapchain.color_space`) and by media pipelines (`media.video`). No engine-internal aliasing; one enum, one meaning.

### HDR and wide-color capabilities

`hdr` carries the union of what the OS publishes:

- `peak_luminance_nits`, `avg_luminance_nits`, `min_luminance_nits`, guarded by `has_luminance` — **absolute** panel luminance in nits, populated only where the OS publishes absolute figures: `DXGI_OUTPUT_DESC1` on Windows, `wp_color_manager_v1` luminance ranges on Wayland, `Display.HdrCapabilities.getDesiredMaxLuminance` (and the min/average pair) on Android. macOS / iOS do **not** publish absolute nits, only EDR ratios (below), so `has_luminance` is false there. The engine never multiplies a ratio by a guessed reference white to synthesize a nit figure (P1, MEL-ENGINE-VIII).
- `edr_reference`, `edr_max_potential`, `edr_max_now`, guarded by `has_edr` — Apple's EDR triplet as **unitless component-value ratios** over SDR reference white (current reference, headroom ceiling under best conditions, headroom available right now), from `NSScreen` / `UIScreen`'s `maximum{,Potential,Reference}ExtendedDynamicRangeColorComponentValue`. They are ratios, not nits — naming them `*_nits` would be the silent corruption MEL-ENGINE-VIII forbids. The triplet drifts with battery / thermals / ambient; the value is a snapshot, and the `configuration_changed` event re-fires when it shifts beyond the platform's quantum.
- `supported_color_spaces[]` — subset of `Mel_Color_Space` the OS will accept for direct scanout to this display.
- `mastering_primaries_support` — `Static | Dynamic | None`. `Static` means HDR10 metadata is honored (DXGI `DXGI_HDR_METADATA_HDR10`, Vulkan `VK_EXT_hdr_metadata`); `Dynamic` adds HDR10+ / Dolby Vision (`DXGI_HDR_METADATA_HDR10PLUS`, `VK_EXT_hdr_metadata_plus`).
- `tone_mapping_owner` — `Display | Compositor | Application`. Tells the app whether to render reference-white-correct PQ and let the panel tonemap, or whether the OS compositor will tonemap on its behalf (macOS EDR), or whether the app is fully responsible (exclusive-fullscreen flip on Windows).

---

## Event stream

`Mel_Display_Event` flows through the platform reactor source:

- `display_added` — hot-plug. New handle attached to the system list.
- `display_removed` — unplug. Handle generation rolls. Surfaces currently anchored to the removed display fire `display_migration` (see `platform.surface`).
- `configuration_changed` — resolution change, refresh-mode change, VRR toggle, HDR enable / disable, ICC profile change, scale-factor change, virtual-desktop reposition. The event carries a bitset naming which fields moved so handlers do not re-read the whole descriptor when only luminance shifted.
- `power_state_changed` — display went `PoweredOff` / `Dimmed` / `Idle` / back to `Active`. Distinguished from `display_removed`: the handle stays valid; the panel is dark. Apps honoring MEL-ENGINE-VI down-shift work when every display they target reports non-`Active`.

Events are coalesced per display per frame on the platform reactor's pump; a burst of OS notifications (Windows `WM_DISPLAYCHANGE` + `WM_DPICHANGED` + `DBT_DEVNODES_CHANGED` storm during a docking event) collapses to one event per affected display with the union of changed fields. Coalescing is a convenience; the native handle remains the P2 escape for apps that want the raw OS storm.

---

## Per-platform lowerings

- **Windows.** `IDXGIOutput6` enumerated per `IDXGIAdapter`; `DXGI_OUTPUT_DESC1` carries native rect, rotation, attached-monitor handle, color space, luminance triple, mastering metadata support. ICC profile resolved through `GetDeviceGammaRamp` / Windows Color System per-monitor profile lookup. Hot-plug arrives via `IDXGIFactory::RegisterAdaptersChangedEvent` plus `WM_DISPLAYCHANGE`.
- **Linux — Vulkan KMS.** `VK_KHR_display` where granted (typically headless / direct-mode renderers, kiosks, embedded). Most desktop Vulkan runtimes do not expose `VK_KHR_display`; on those, display info comes from the windowing system below.
- **Linux — Wayland.** `wl_output` (output advertisement) plus `xdg-output-unstable-v1` / `xdg-output-v2` for logical position and scale; the Wayland color-management protocol (stabilizing 2025-2026) for HDR / color-space surface. The compositor is authoritative; the engine never bypasses it.
- **Linux — X11.** XRandR 1.5+ (`RRGetScreenResourcesCurrent`, `RRGetOutputInfo`, `RRGetCrtcInfo`). EDID parsed from the `EDID` output property for physical size and panel name.
- **macOS.** `NSScreen` enumeration; `NSScreen.frame` / `.backingScaleFactor` / `.colorSpace` / `.maximumExtendedDynamicRangeColorComponentValue` / `.maximumPotentialExtendedDynamicRangeColorComponentValue` / `.maximumReferenceExtendedDynamicRangeColorComponentValue`. Mode list via `CGDisplayCopyAllDisplayModes`. Hot-plug via `NSApplication.didChangeScreenParametersNotification`.
- **iOS / iPadOS / visionOS.** `UIScreen` (deprecated iOS 16+; the modern path is `UIScene.windowScene.screen`). External-display attach via `UIScene` connection notifications. visionOS handles its compositor surface through `cp_layer_renderer` and synthesizes one head-mounted display entry (see `xr.md`).
- **Android.** `Display` + `DisplayManager` via JNI; `Display.Mode[]` for refresh modes, `Display.HdrCapabilities` for HDR, `Display.getColorSpaces` for color spaces. Power state via `Display.getState` (`STATE_ON | _OFF | _DOZE | _DOZE_SUSPEND | _VR | _ON_SUSPEND | _UNKNOWN`).
- **Web.** Synthetic single-display entry — see below.

---

## Relationship to GPU (`Mel_Gpu_Adapter`)

The GPU adapter exposes `adapter_displays(a) → Mel_Display[]` — the subset of system displays the adapter can drive. The display handles themselves are minted by `display`; the adapter list is a filtered view. Apps that ignore displays get the system-default association (the swapchain lands on whichever display the surface's window currently occupies); apps that care call `swapchain_create({ ..., target_display: Some(d) })` to pin direct-output composition (Windows exclusive-fullscreen flip, Vulkan `VK_KHR_display_swapchain` where granted, Metal direct-mode CAMetalLayer with `displaySyncEnabled` on a chosen `NSScreen`).

Display handles invalidate together with their adapter on the GPU's `adapter_removed` event (gpu-rhi.md §5.1). The cause is GPU-side — eGPU disconnect, hot-unplug, DXGI adapter renumber — so the event lives in the GPU module. The *consequence* — displays attached to that adapter disappearing — propagates here as `display_removed` events on each affected handle. The cross-module sequencing is contractual: GPU fires `adapter_removed` first, `display` fires `display_removed` immediately after (same reactor tick), so a handler reading `display` state after observing `adapter_removed` sees a consistent post-removal view (MEL-ENGINE-IX).

Use-after-removal is a loud `alive()` failure on `display_describe`, on `swapchain_create` with the dead handle as `target_display`, and on any other consumer — never a dangling-pointer crash (MEL-ENGINE-VIII).

---

## WebGPU and the privacy-limited synthetic display

Browsers expose the containing screen of the rendering canvas only, and even that with privacy guards. `display` reports exactly one synthetic entry on the Web target:

- `name = ""` (the browser does not publish monitor names to script).
- `connector = Unknown`.
- `native_resolution = Some({ screen.width, screen.height })` in CSS pixels × `devicePixelRatio`. The browser may quantize these for fingerprinting resistance.
- `physical_size_mm = None`.
- `refresh_modes[] = [{ width_px, height_px, refresh_mhz: round(screen.refreshRate) , interlaced: false }]` where `screen.refreshRate` is published (Chrome 121+); a single entry, the current mode only.
- `vrr_range = None` (no browser API).
- `hdr` — populated from `window.matchMedia('(dynamic-range: high)')` plus `screen.colorDepth`; `peak_luminance_nits` absent unless the page is granted the experimental HDR metadata surface; `supported_color_spaces` reduced to `sRGB` plus `Display_P3` and `Rec_2020` where the GPU canvas configuration admits them.
- `icc_profile = None`.
- `state = Active` (browsers do not publish display power state to script).
- `position_virtual = None`.
- `scale_factor = devicePixelRatio`.
- `native_handle = null`.

This is emulation under P1: the API shape is preserved, the fields the browser withholds are honestly `None` or empty, and `caps.display.privacy_tier = web_limited` lets power users branch when the difference matters. The engine never synthesizes a plausible-looking luminance figure to fill the gap (MEL-ENGINE-VIII).

---

## Native-handle escape (P2)

`display_native_handle(d) → Mel_Display_Native_Handle` returns the tagged-union platform object. Apps reaching past the engine — to drive `IDXGIOutput6::TakeOwnership`, to read EDID bytes directly, to consume vendor-private Apple display-link APIs through `CVDisplayLink`, to subscribe to `DisplayManager.DisplayListener` JNI callbacks the engine does not relay — take the handle and call native code. The engine does not police that path; it does invalidate the tagged union on `display_removed` so a stale native pointer is at least detectable as a `Lost`-tagged variant before the app dereferences it.

---

## Implementation status

The public surface lives in `<display/display.h>`: `Mel_Display`, `Mel_Display_Descriptor`, the `Mel_Color_Space` enum, the HDR struct, the event types, and the API (`mel_display_init` / `_shutdown` / `_refresh` / `_count` / `_list` / `_describe` / `_alive` / `_equal` / `_native_handle` / `_poll_events`).

The module is a **producer**: every GPU/surface coupling above (`adapter_displays`, `target_display`, `display_migration`, the `adapter_removed → display_removed` sequencing) is a downstream consumer that imports this handle once those modules exist. None is a build dependency; `display` compiles and runs standalone.

Architecture: a portable registry/diff core (`src/display.c`) owns the slotmap, keys entries by a backend-supplied stable id, and on `refresh()` diffs the live set — preserving handles for survivors (identity persists), rolling generations for the departed, and emitting events with a per-field changed-mask (collapsing to `power_state_changed` when only `state` moved). Each platform implements one seam, `mel_display__enumerate` (`src/display_backend.h`), selected by the build's `src/<platform>/` and `src/<runtime>/` chain.

Per-platform lowering status:

- **macOS** (`src/macos/`) — implemented and verified on hardware (`apps/display-gui`). `NSScreen` + Core Graphics, exact-millihertz refresh, P3 gamut, EDR ratios, ICC bytes, `NSScreen*` native handle. `scale_factor` is `backingScaleFactor`, i.e. the backing scale of the *current mode* — 1.0 for a 1× mode (e.g. a 2560×1600 framebuffer where pixels == points), 2.0 for a HiDPI "Retina" mode — so it tracks the user's resolution choice. `edr_max_now` is live headroom and rises above 1.0 only while EDR content is on screen or at lower SDR brightness (the inspector's EDR-force button demonstrates this).
- **iOS** (`src/ios/`) — implemented via `UIScreen`. Single main screen; external screens are a follow-up via `UIScene` notifications.
- **Linux** (`src/linux/`) — implemented via XRandR 1.5+. Wayland (`wl_output` + color-management) and Vulkan-KMS (`VK_KHR_display`) remain follow-ups.
- **Android** (`src/android/`) — implemented via `DisplayManager` + `Display` through JNI (`mel_platform_android_env`).
- **Windows** (`src/win32/`) — implemented via `IDXGIOutput6` / `DXGI_OUTPUT_DESC1`.
- **Web** (`src/emscripten/`, plus a `src/wasi/` no-display stub) — the synthetic privacy-limited entry below, read from `screen.*` / `matchMedia` / `devicePixelRatio`.

Deferred (tracked in `todo.md`): reactor-source **push** delivery (today events are produced by the diff and drained with `mel_display_poll_events`; no reactor source or OS hot-plug notification is wired yet); the per-platform field gaps (connector kind beyond Internal/Unknown, VRR range, honest mastering-metadata detection); an injectable enumerate seam for unit-testing the diff; and all the GPU/surface coupling above.
