# display — todo

Status: the portable registry/diff core, the full public type surface, and the
macOS (`NSScreen` + Core Graphics) lowering are implemented and verified on real
hardware. What remains, roughly in dependency order.

## Event delivery (next increment)
- [ ] Wire a `Mel_Reactor_Source` so events are pushed on the reactor pump rather
      than pulled via `mel_display_refresh` + `mel_display_poll_events`. The diff
      logic already produces the events; only the push path is missing.
- [ ] macOS hot-plug / reconfigure: observe `NSApplication
      didChangeScreenParametersNotification` (and/or `CGDisplayRegister
      ReconfigurationCallback`) and drive `refresh()` from it.
- [ ] Per-display per-frame event coalescing on the pump, as the spec requires
      (collapse a docking-event notification storm to one event per display with
      the union of changed fields).

## Per-platform lowerings — implemented, with gaps

All six `mel_display__enumerate` backends exist and compile against their real
toolchains (macOS full build + on-hardware run; iOS Xcode SDK; Android NDK;
Windows mingw/DXGI; Linux X11+Xrandr; Web emscripten + wasi). Only macOS is
runtime-verified — the others are compile-verified and review-checked, awaiting a
run on each platform.

- [x] **Windows** (`src/win32/`) — `IDXGIOutput6` / `DXGI_OUTPUT_DESC1`: rect,
      desktop position, color space, absolute luminance triple, mastering support;
      modes via `GetDisplayModeList`; stable id = FNV of `DeviceName`.
  - [ ] DPI / scale_factor (needs `GetDpiForMonitor`, `-lshcore`); currently 1.0.
  - [ ] ICC via Windows Color System; connector kind; hot-plug. P2 native access
        today is `mel_display_win32_device_name` (the `\\.\DISPLAYn` path); a live
        `IDXGIOutput6*` accessor (re-acquire by matching the device path during
        enumeration, AddRef'd, caller releases) is the follow-up.
- [x] **Linux/X11** (`src/linux/`) — XRandR 1.5+; physical size + name from output
      info; position/current-res from CRTC; modes from `XRRModeInfo`; connector
      inferred from output-name prefix; stable id = `RROutput`.
  - [ ] EDID parse for a better panel name; per-output scale; `_ICC_PROFILE` atom.
- [ ] **Linux/Wayland** — `wl_output` + `xdg-output` for position/scale; the
      color-management protocol for HDR. Compositor is authoritative. (Not started;
      X11 covers XWayland sessions in the meantime.)
- [ ] **Linux/Vulkan KMS** — `VK_KHR_display` where granted (kiosk / direct mode).
- [x] **Android** (`src/android/`) — `DisplayManager` + `Display` through JNI via a
      `MelodyDisplay` Java helper (mirrors the midi bridge; `Context` from
      `ActivityThread.currentApplication`). Modes, density→scale, `getState`,
      `HdrCapabilities` luminance, wide-gamut → P3.
  - [ ] `getColorSpaces` for a fuller color-space set; runtime verification on
        device. P2 native access today is `mel_display_android_display_id`; a
        `Display` JNI global-ref accessor is the follow-up.
- [x] **iOS / iPadOS** (`src/ios/`) — `UIScreen`: `nativeBounds`, `availableModes` +
      `maximumFramesPerSecond`, EDR headroom (iOS 16+), P3 via `traitCollection`.
  - [ ] Migrate off deprecated `UIScreen.screens`/`mainScreen` to the
        `UIScene.windowScene.screen` path; external-display attach via `UIScene`
        notifications. visionOS HMD entry (see `xr.md`).
- [x] **Web** (`src/emscripten/`, `src/wasi/` stub) — synthetic privacy-limited
      entry from `screen.*`, `matchMedia('(dynamic-range: high)' / '(color-gamut: …)')`,
      `devicePixelRatio`. wasi returns no displays (DOM-less).
  - [ ] `caps.display.privacy_tier = web_limited` once a caps surface exists.

## macOS lowering — fill gaps
- [x] ~~scale_factor reads 1.0 because of a missing window-server session~~ —
      **disproven** via `apps/display-gui`. `scale_factor` = `backingScaleFactor`
      = the current mode's backing scale: 1.0 for a true 1× mode (2560×1600 where
      pixels == points, confirmed by `CGDisplayModeGetPixelWidth`/`GetWidth`), 2.0
      for a HiDPI mode. Correct as-is. EDR `now` is live headroom (1.0 in SDR,
      rises with EDR content / lower brightness — the inspector forces it).
- [ ] Open design choice: keep `scale_factor` as the *current mode's* backing
      scale (matches spec wording, tracks the user's resolution), or report the
      panel's *intrinsic* scale (always 2.0 on a Retina panel regardless of mode).
      Currently the former.
- [ ] Connector kind beyond `Internal`/`Unknown` — walk IORegistry
      (`IODisplayConnect`) to distinguish HDMI / DisplayPort / USB-C.
- [ ] VRR range — `vrr_range` is currently always absent on macOS; expose
      ProMotion's adaptive range where derivable.
- [ ] `mastering_primaries_support` is heuristic (Static when EDR-capable).
      Detect HDR10 / HDR10+ / Dolby Vision support honestly.

## Cross-module coupling (blocked on consumers that do not exist yet)
- [ ] `Mel_Gpu_Adapter::adapter_displays(a)` — adapter-filtered view of system
      displays. Lives in the GPU module; imports this handle.
- [ ] `swapchain_create({ target_display })` direct-output composition.
- [ ] `adapter_removed` -> `display_removed` contractual same-tick sequencing
      (GPU fires first). Today only our own hot-unplug invalidation path exists.
- [ ] `display_migration` from `platform.surface` when an anchored surface's
      display is removed.
- [ ] `Mel_Platform_Instance` as the real owner of the display slotmap (the spec's
      named owner). Today the registry is module-local statics.

## Testability / hardening
- [ ] Make `mel_display__enumerate` injectable (function-pointer seam) so the
      portable add/remove/change/identity-persistence diff can be unit-tested with
      a fake backend, independent of real hardware and the fork harness.
- [ ] Test harness: a no-fork / `posix_spawn` re-exec mode so Cocoa-touching
      enumeration can be unit-tested instead of only probed (see
      `tools/test/src/runner.c`).
- [ ] Fixed registry caps (`REGISTRY_CAP 32`, `EVENTS_CAP 128`, `MAX_MODES 64`)
      are arbitrary; overflow is logged, not silent, but revisit the limits.

## Open design question
- [ ] EDR field naming. The spec names the triplet `edr_*_nits` and lists
      `peak/avg/min_luminance_nits`; Apple publishes EDR as unitless component-value
      ratios and no absolute nits. Implemented as unitless `edr_reference` /
      `edr_max_potential` / `edr_max_now` with `has_luminance = false` on macOS, to
      avoid putting a ratio in a `_nits` field (MEL-ENGINE-VIII). Resolve the unit
      story before other HDR-capable lowerings copy the convention.
