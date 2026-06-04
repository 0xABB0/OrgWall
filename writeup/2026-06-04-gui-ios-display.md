# GUI — UIKit gpu_view + iOS debug stacktrace (hello-gpu windowed iOS path)

## Work done

Closed the two sibling-module gaps the metal-ios agent flagged (its writeup `2026-06-03-gpu-metal-ios.md`,
"Cross-module blockers"). The Metal-iOS render core already rendered+read-back on the simulator; the
windowed `hello-gpu ios --gpu=metal` path was blocked only by (1) the absent uikit `gpu_view` and (2) the
absent iOS debug stacktrace backend. Both now exist; `hello-gpu ios --gpu=metal` links and packages with
zero missing symbols.

### uikit gpu_view (`modules/gui/src/uikit/gpu_view.m`, new)
UIKit analogue of `cocoa/gpu_view.m`. A `MelGpuView : UIView` whose `+layerClass` returns
`CAMetalLayer`, so the view is natively layer-backed by the exact layer class
`modules/gpu/src/metal/ios/surface.m` consumes (its `[view.layer isKindOfClass:CAMetalLayer]` branch
reuses the layer directly — no sublayer, no sublayer-frame tracking).
- `mel_gpu_view_create_opt` mirrors `cocoa/gpu_view.m` and the uikit `canvas.m`: `mel_gui__node_new`
  then install via `mel_gui__ios_install_child`, which stores `n->native = CFBridgingRetain(view)`.
- `mel_gpu_view_surface` returns `n->native` — the retained `UIView*`, precisely the handle the iOS
  surface `__bridge`-casts to `UIView*`. Identical contract to the cocoa `NSView*` return.
- `layoutSubviews` fires `on_resize(handle, w, h, user)` from `self.bounds.size`, gated on a
  `last_w/last_h` change (mirrors `MelViewController.viewDidLayoutSubviews`). This is the iOS analogue
  of cocoa's `setFrameSize`; it is what drives `gpu_host.c`'s `window_resized` → surface+swapchain
  creation and the render-source loop.
- Touch handlers (`touchesBegan/Moved/Ended`) forward to the `pointer` callbacks, matching `canvas.m`
  for composability (MEL-ENGINE-IX).
- Declared `MelGpuView` in `uikit.h` (mirrors `MelGuiGpuView` in `macos.h`), adding `last_w/last_h`.
- `view.opaque = YES; backgroundColor = blackColor` so an un-presented frame is honest black, not
  undefined.

No `gui/build.c` change was needed: `mel_sources(... WHEN(.backend = "uikit"), "src/uikit/*.m")`
already globs the new file, and the IOS link line already pulls `QuartzCore` (CAMetalLayer).

### iOS debug stacktrace (`modules/debug/src/ios/stacktrace.c`, new; `debug/build.c` gate)
Verbatim port of `macos/stacktrace.c` (pure Darwin `backtrace()` + `dladdr()`), with the guard
switched to `#if !MEL_PLATFORM_IOS`. Added the gate `mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)),
"src/ios/*.c")` (pure C; no `.m`, unlike the macOS line). `gpu` depends on `debug`, so this symbol now
links transitively into every iOS target that pulls gpu (hello-gpu, gpu-metal test).

### Shim removal (gpu-test edit — FLAGGED)
`modules/gpu/test/ios_stacktrace_shim.c` was the metal-ios agent's temporary stand-in for the missing
debug iOS backend (its Kludge #1, "delete it the moment the debug module grows its iOS lane"). Deleted
the file and removed its build line `mel_sources(metaltest, WHEN(.platforms = MEL_ON(IOS)),
"test/ios_stacktrace_shim.c")` from `modules/gpu/build.c`. The gpu-metal iOS test now links the
canonical `debug/src/ios/stacktrace.c` via its `gpu` dependency. Re-verified: gpu-metal iOS links with
no duplicate/missing symbol and runs 4/4 on the sim.

## Build & run results

- `./nob build hello-gpu macos --gpu=metal` → LINK OK, `.app` packaged (gui regression guard).
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-metal macos --gpu=metal`
  → **4/4**, run twice (before and after all edits).
- `./nob build hello-gpu ios --gpu=metal` → LINK OK, `.app` packaged. `gpu_view.o` is in
  `libgui.a`; `ios/stacktrace.o` is in `libdebug.a`. Both former link blockers resolved.
- `./nob compile gpu-metal ios --gpu=metal` → LINK OK without the shim.
- gpu-metal iOS readback on the booted sim (`SIMCTL_CHILD_MEL_TEST_NOFORK=1 xcrun simctl spawn`)
  → **4/4**, exit 0, now resolving `mel__platform_stacktrace_capture` from the real debug backend.
- hello-gpu installed + launched on iPhone 16 sim (iOS 18.6): the UIKit window opens and the native
  gui menu renders (screenshot captured). The app stays alive with **zero crash**, **zero error/assert
  markers** across the full session console; Metal instance+device come up
  (`Apple iOS simulator GPU`). Display mode `393x852@3x`.
- All new/edited files clang-format clean (Apple clang-format 17).

## Honest blocker: the windowed-triangle screenshot

hello-gpu opens a **menu** of graphical demos; each button's `on_click` calls `gpu_host_open`, which
on iOS pushes a new view controller hosting the gpu_view where the triangle renders. To capture the
triangle I must tap "hello-triangle" first. **Every synthetic-input route available in this sandbox
failed to activate the in-device UIButton:**
- `osascript` System Events `click at {x,y}` → error -25204 (accessibility/event-tap denied).
- `CGEventPost` (both `kCGHIDEventTap` and `kCGSessionEventTap`, with clickState) at the AX-reported
  button center (host-screen rect read directly from the Simulator's AX bridge: pos 1099,435 size
  361×36 → center 1279,453) → events **reach the UIWindow** (unified log shows `Sending UIEvent ... to
  window` / `send control actions` at each tap timestamp) but never resolve to a `UIButton`
  touch-up-inside; `gpu_host_open` never fires (no surface/swapchain console line).
- `AXPress` and AppleScript `click` on the bridged `AXButton` "hello-triangle" → reported success,
  no effect (the Simulator's AX mirror does not drive the in-device control's target-action).

So the windowed gpu_view path is **built, wired, and linked**, the app **launches and runs crash-free**,
and the Metal render core is **proven on the sim** (4/4 readback) — but the on-screen triangle could not
be screenshot-proven because this headless/sandboxed session cannot inject a touch the simulated
UIButton accepts. The gap is environmental (synthetic input into the simulator), not in the gpu_view or
debug code. The one code-level link from gpu_view → surface → swapchain that remains pixel-unproven is
the same one the metal-ios agent flagged (its Kludge #2); the rest of the chain is now closed.

## Kludges / debt (confessed — bar zero, MEL-ENGINE-VIII)
1. **Windowed present pixel-unproven on the sim.** As above: synthetic tap blocked → cannot screenshot
   the pushed triangle screen. Headless readback (4/4) proves render+readback; the `CAMetalLayer ->
   nextDrawable -> present` windowed path is exercised only by code-path inspection, not a captured
   frame. To close: run on a real device, or from a session with synthetic-input/accessibility
   entitlements (or an XCUITest that taps the button), then `simctl io screenshot` the triangle.
2. **gpu_view loud-reject is downstream, not in-place.** `gui` depends on neither `log` nor `debug`
   (adding either is outside this task's file ownership and would perturb every gui platform). Cocoa's
   `mel_gpu_view_surface` likewise returns NULL silently on a missing node; I kept that contract. The
   loud rejection of misuse (null/non-UIView handle) fires in `metal/ios/surface.m`'s
   `mel_gpu_surface_create`, which already `mel_log_error`s — exactly where the misuse is actionable
   (the gpu module owns the log dep). Net: misuse is loud, but the log lives one module downstream of
   the gui surface accessor.

## CLAUDE.md suggestions (recommendations only — not applied)
- Note the iOS-sim windowed-verification limitation: synthetic taps into a booted simulator from a
  headless/SSH session do not activate UIKit controls (events reach the window but no touch-up-inside);
  on-screen UI proof needs a real device, an entitled session, or an XCUITest. Pairs with the existing
  `MEL_TEST_NOFORK=1` + `simctl spawn` sim-test note.

## Suggestions
- A tiny XCUITest target for hello-gpu (tap "hello-triangle", assert the gpu_view screen, screenshot)
  would make the windowed iOS path CI-verifiable and retire Kludge #1.
- The metal-ios agent's suggestion stands: teach the `nob test` driver an iOS-simulator lane
  (`simctl spawn` with `SIMCTL_CHILD_MEL_TEST_NOFORK=1`) so `./nob test gpu-metal ios` works end-to-end.
- `gui`'s NULL-return-on-misuse convention (cocoa and now uikit) is quiet relative to MEL-ENGINE-VIII.
  Consider letting `gui` depend on `log` for backend-side loud rejection at the point of misuse.
