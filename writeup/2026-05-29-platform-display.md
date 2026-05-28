# platform.display — initial module + macOS lowering

> Continued later the same day: renamed to the `display` module, relocated the
> spec into the module folder, and implemented the remaining five platform
> lowerings. See "Session 2" at the end.

## Work done

Implemented `docs/platform-display.md` as a standalone module. The spec is a leaf of
the gpu-rhi v4 architecture, but it is the *producer*: every coupling it names
(`adapter_displays`, `swapchain_create{target_display}`, `display_migration` from
`platform.surface`) is a downstream consumer that does not exist yet. None is a
dependency, so the module is buildable end-to-end on its own. Confirmed by reading
`modules/gpu` (no adapter/output/color-space concept) and the platform module (no
`Mel_Platform_Instance`, no surface module).

- `modules/platform/include/platform/display.h` — full public surface: `Mel_Color_Space`
  (the spec parks the enum here), `Mel_Color_Icc_Profile`, the descriptor + HDR structs,
  `Mel_Platform_Display` handle over `Mel_SlotMap_Handle`, the `{value, status}` describe
  result, the event enum + changed-field bitset, and the tagged native-handle union.
- `modules/platform/src/display.c` — portable registry/diff core. Owns a slotmap of
  displays (module-local, since `Mel_Platform_Instance` does not exist), keys entries by a
  backend-supplied stable id, and on `refresh()` diffs against the live set: surviving
  displays keep their handle and generation (identity persists across config changes, per
  spec), new ones emit `display_added`, gone ones are removed (generation rolls) and emit
  `display_removed`, changed ones emit `configuration_changed` with a field bitset —
  collapsing to `power_state_changed` when only `state` moved. ICC bytes are registry-owned
  and freed on update/removal/shutdown. Loud-not-fatal on dead handles: `describe` logs and
  returns `INVALID_HANDLE`, `native_handle` returns the `Lost` variant (`mel_assert` is a
  no-op stub today, so loud = log + status, not crash).
- `modules/platform/src/macos/display_macos.m` — the `__enumerate` hook via `NSScreen` +
  Core Graphics. Native res from the max pixel mode; `refresh_modes` from
  `CGDisplayCopyAllDisplayModes` with refresh in exact millihertz (59.940/47.950 cadences
  verified); physical size from `CGDisplayScreenSize`; connector Internal/Unknown via
  `CGDisplayIsBuiltin`; state from `CGDisplayIsActive`/`IsInMirrorSet`; EDR triplet and P3
  gamut from `NSScreen`; ICC bytes from `colorSpace.ICCProfileData`; `NSScreen*` as the P2
  native handle.
- `modules/platform/src/display_backend.h` — the `__enumerate` seam; portable `display.c`
  ships a no-display stub for non-Apple targets (honest P1 emulation until those lowerings
  land).
- `modules/build.c` — `display_macos.m` excluded off macOS; added `-framework CoreGraphics`.
- `modules/platform/test/display_test.c` — three Cocoa-free contract tests (dead/null/equal).
  All green.
- `apps/display-probe/` — in-process verification app (the fork-based test harness cannot
  exercise Cocoa; see kludges). Enumerates this MacBook correctly: built-in 3456×2234,
  ProMotion 120→47.95 Hz, P3, 16× EDR potential, 4 KB ICC, `NSScreen*` handle.

### Adjacent bug fixed (not in scope, surfaced by this work)

`modules/log/src/log.c` — `mel_log_level_register` and the sink registry grew from empty
with `mel_realloc(heap, NULL, …)`, which violates the allocator contract (`mel__realloc`
asserts `ptr != NULL`; new allocations must use `mel_alloc`). It never fired before because
nothing in the test binary referenced `log`, so the linker dropped `log.o` and its
constructor never ran; `display.c`'s `mel_log_error`/`warn` pulls `log.o` in, running
`mel__log_init`, which hit the assert in debug. Release hid it (`assert` compiled out,
`realloc(NULL,n)` degrades to `malloc`). Fixed both sites to `alloc`-when-empty,
`realloc`-to-grow. This unblocked the whole debug test suite.

## Kludges / debt

- **Events are pull-only, not reactor-pushed.** The spec routes events through a
  `Mel_Reactor_Source` coalesced on the reactor pump. I implemented the diff and a
  `poll_events` drain, but did not wire the reactor source or the macOS hot-plug
  notification (`didChangeScreenParametersNotification`). Events only materialize when the
  app calls `refresh()`. The diff logic is the reusable half; the push delivery is deferred.
- **No mock seam for the registry/diff in unit tests.** `__enumerate` is a link-resolved
  free function, so the portable diff logic (add/remove/change/identity-persistence) is only
  exercised by the probe, not by the fork harness. A function-pointer seam would let the
  diff be unit-tested with a fake backend.
- **macOS scale/EDR are GUI-session-dependent.** Verified (raw AppKit, independent of our
  code): with no window-server session, `backingScaleFactor` reads 1.0, `maxEDR` 1.0,
  `refEDR` 0.0. The lowering reads the canonical fields the spec names; values are correct
  only inside a process with a window-server connection. Not a logic bug; a runtime caveat.
- **Field-naming deviation from the spec (deliberate, needs Gabbo's ruling).** The spec
  names the EDR triplet `edr_reference_nits` / `edr_max_potential_nits` / `edr_max_now_nits`
  and lists `peak/avg/min_luminance_nits`. Apple publishes EDR as *component-value ratios*,
  not nits, and does not publish absolute panel luminance. Populating a `_nits` field with a
  ratio is exactly the silent corruption MEL-ENGINE-VIII forbids, so I named them
  `edr_reference` / `edr_max_potential` / `edr_max_now` (unitless) and left
  `has_luminance = false` on macOS (P1: omit, never synthesize). If the literal spec names
  are wanted, the unit story needs resolving first.
- **Registry caps are fixed** (`REGISTRY_CAP 32`, `EVENTS_CAP 128`, `MAX_MODES 64`).
  Overflow is logged, not silent (per "no silent caps"), but the caps are arbitrary.

## CLAUDE.md suggestions (recommendations only)

- The repo CLAUDE.md does not state that the `docs/` specs are aspirational (gpu-rhi v4) vs.
  what is implemented. A one-line pointer ("specs in `docs/` describe target architecture;
  `modules/` is ground truth") would have saved an exploration pass.

## Suggestions

- **Test harness needs a no-fork / re-exec mode.** `tools/test/src/runner.c` forks per test
  on POSIX; any test touching AppKit (or other fork-unsafe frameworks) aborts in the child.
  A `posix_spawn` re-exec path, or an in-process opt-in flag per test, would let
  platform/Cocoa code be unit-tested. Today such code can only be verified by a standalone
  app.
- **`Mel_Platform_Instance` is the missing owner.** Several specs (display, surface, sensors)
  name it as the owner of system handles. Defining it minimally would give the platform
  modules a real home instead of module-local statics.
- **Next lowerings:** Windows (`IDXGIOutput6`) is the most fully specified and has
  scaffolding (`win32_globals`, recent windows-build commit); Web (synthetic privacy-limited
  entry) is the smallest and exercises the all-`None` honesty path.
- **Reactor source + hot-plug** is the natural next increment on this module, turning
  `poll_events` into pushed, coalesced events.

---

# Session 2 — `display` module, spec relocation, five more lowerings

## Work done

- **Renamespace + relocation.** Moved the module from `modules/platform/...` to
  `modules/display`, renamespaced (`Mel_Platform_Display`→`Mel_Display`,
  `mel_platform_display_*`→`mel_display_*`, `MEL_PLATFORM_DISPLAY_*`→`MEL_DISPLAY_*`;
  `Mel_Color_*` kept), header now `<display/display.h>`. Per Gabbo's choice
  ("renamespace to display"). Added `readme.md` + `todo.md`.
- **Spec moved into the module.** `docs/platform-display.md` → `modules/display/spec.md`,
  renamespaced, with the `platform.display` references across sibling docs
  (gpu-rhi, platform-surface, frame-pacing, media-video, render-graph, xr) updated
  mechanically. Reconciled the EDR units in the spec body (the `_nits` lie → honest
  unitless ratios + `has_luminance`), noted module-local ownership until
  `Mel_Platform_Instance` exists, and appended an implementation-status section.
- **Five lowerings**, each a single `mel_display__enumerate` behind the existing
  seam; the portable core is untouched:
  - `src/ios/display_ios.m` — `UIScreen` (nativeBounds, availableModes +
    maximumFramesPerSecond, EDR headroom, P3 trait).
  - `src/win32/display_win32.c` — `IDXGIOutput6`/`DXGI_OUTPUT_DESC1` via the DXGI
    C/COM API (`COBJMACROS`); absolute luminance, HDR10 color space, modes, FNV id.
  - `src/linux/display_linux.c` — XRandR 1.5+ (output info, CRTC, mode table),
    connector inferred from output name.
  - `src/android/display_android.c` + `MelodyDisplay.java` — `DisplayManager` over
    JNI, mirroring the midi Java-bridge / `ActivityThread` context pattern.
  - `src/emscripten/display_emscripten.c` + `src/wasi/display_wasi.c` — synthetic
    privacy-limited browser entry; wasi returns none.
- **Build wiring.** Deleted the `#if !__APPLE__` stub and the redundant
  `display_macos.m` excludes (the `src/<platform>/` chain auto-gates each backend).
  Added `-ldxgi -ldxguid` (win32) and `-lX11 -lXrandr` (linux), kept `CoreGraphics`.
- **Verification.** macOS: full build + tests green, on-hardware probe unchanged.
  iOS / Android / win32 / linux / emscripten / wasi: compile-verified against their
  real SDKs (`xcrun iphoneos`, NDK r28 clang, `x86_64-w64-mingw32-gcc`, host clang
  + homebrew X11/Xrandr, `emcc`, host clang). Installed `libxrandr` headers locally
  to syntax-check the Linux backend.

## Kludges / debt

- **Only macOS is runtime-verified.** The other five compile and pass review but
  have not run on their platforms. Field mappings (esp. Android JNI signatures,
  DXGI color-space interpretation, XRandR refresh math) want a real run.
- **iOS uses deprecated `UIScreen.screens`/`mainScreen`** (warns under iOS 16/26).
  Runtime-fine; the modern `UIScene.windowScene.screen` path is the follow-up.
- **P2 native handle is `id`-only on win32/android/linux** — storing a retained
  COM/JNI/Xlib pointer with a lifetime tied to slotmap removal isn't wired, so
  `native_handle.ptr = NULL` there. macOS/iOS carry the real `NSScreen*`/`UIScreen*`.
- **Per-platform field gaps** (all in `todo.md`): win32 DPI/ICC/connector, Android
  colorspaces, Linux EDID/scale/ICC, Wayland + Vulkan-KMS not started.
- **iOS full-lib build is blocked upstream**, not by this module:
  `modules/gpu/src/metal/swapchain.m` includes `AppKit/AppKit.h` unconditionally,
  which doesn't exist on iOS. Worth fixing separately.

## Suggestions

- Fix the `gpu/metal/swapchain.m` AppKit-on-iOS include so the iOS target links at
  all (guard the AppKit path, or split the macOS-only bits).
- The deprecation and the "only macOS runs" gap argue again for the no-fork test
  harness mode (Session 1 suggestion) plus per-platform smoke runs in CI.
