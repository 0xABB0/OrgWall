# Desktop Window Capabilities — gui.desktop

## Problem Statement

The cross-platform Stage abstraction (PRD 03) intentionally exposes only the lowest common denominator of top-level window behavior. But desktop platforms can do much more: transparency, framelessness, click-through, custom z-levels, multi-monitor positioning, no-shadow rendering, taskbar control, focus-ignore. I want those capabilities for things like:

- A desktop widget that sits transparently above the wallpaper
- A character or sprite that animates outside its window's bounds (the "Norton dog" pattern)
- Floating tool palettes that ignore Z-fighting with main windows
- Click-through overlays for HUD-style information

These features are inherent to desktop platforms and inherently absent from mobile, XR, web, and wearable platforms. Pretending otherwise leads to either a watered-down API or a leaky cross-platform abstraction. I want desktop-rich apps to access full desktop capabilities and to be desktop-only by construction — they fail to build on Android, by design, because they cannot work there.

## Solution

A tier-2 module `gui.desktop` exposes the full desktop window API. It exists on Win32, Cocoa (macOS), X11, and Wayland; it does not exist on Android, iOS, Vision OS, Quest, web, or wearable backends. Apps that include `gui.desktop` are committing to being desktop-targeted; build failures on non-desktop platforms are the correct outcome and require no `#if` in app code.

The desktop window handle is still a `Mel_Gui_Handle` — it is the same type as a stage, the same as any other widget. It differs in three ways: it is created by a desktop-specific function with desktop-specific options, it has desktop-specific runtime operations, and it cannot be created on non-desktop platforms.

For cross-platform apps that want some desktop richness on desktop only, the path is conditional dependency in `build.c`: declare `gui.desktop` as a dependency only when the target is a desktop platform. Build system supports per-platform module sets natively.

## Implementation Decisions

- A new module `gui.desktop` exists only on desktop platforms. Build system enforces this: depending on `gui.desktop` from a target compiled for a non-desktop platform is a build error.
- The module exposes a `Mel_Gui_Desktop_Window_Opt` create struct with rich fields: title, x/y/w/h in absolute screen coordinates across monitors, opacity, z-level, capability flags, callbacks for lifecycle/pointer/focus/keyboard.
- Capability flags include at minimum: `TRANSPARENT`, `FRAMELESS`, `CLICK_THROUGH`, `NO_SHADOW`, `NO_TASKBAR`, `TOOL_WINDOW`, `IGNORE_FOCUS`, `ABOVE_FULLSCREEN`. Additional flags can be added per platform.
- A z-level integer (signed) maps to platform conventions: -1 below normal, 0 normal, +1 floating, +2 popup, +3 tooltip, with higher levels reserved for specific use cases.
- Runtime operations include opacity setting, click-through toggling, level changing, and an input-shape mask (a list of rectangles that compose the hit-testable region; pointer events outside the mask pass through).
- An "overlay" relationship allows pinning a frameless transparent click-through window to a main window at a screen offset. This is the primitive that enables out-of-bounds character animation: the character lives in an overlay window that moves with the main window but extends beyond it.
- Created windows are full `Mel_Gui_Handle`s. All cross-platform handle operations (set_window_pos, set_text, invalidate, request_frames, child attachment) work uniformly.
- The cross-platform `gui/` module knows nothing about `gui.desktop`. `gui.desktop` depends on `gui/` and the relevant platform layers.

## Testing Decisions

A good test of `gui.desktop` verifies behavior visible to user code: that a created window has the requested capability flags applied where supported, that opacity changes are reflected after a call to set opacity, that click-through state propagates to hit testing, that the overlay-pin relationship causes overlay position to track main window position when main moves.

Modules under test: `gui.desktop` directly, with platform stubs or real platform invocation depending on the test environment.

Prior art: none in the current repo. Desktop window tests are platform-dependent; on macOS, the test harness can drive NSWindow operations and observe state.

## Out of Scope

- System tray icons, dock badges, jump lists, menu bar items. These are desktop-only but each is its own concern; they may live in `gui.desktop` later or in sibling tier-2 modules (`gui.desktop.tray`, `gui.desktop.menu`).
- Drag-and-drop, clipboard, system file dialogs. Same as above — desktop concerns, separate PRDs.
- Window snapping zones (Win11 snap groups, macOS Stage Manager interaction). Future work.
- Fallback behavior for apps that conditionally include `gui.desktop`. Apps are expected to handle the build-level conditional themselves via the build system; runtime feature detection is not provided.

## Further Notes

The desktop window handle is intentionally not a separate type — it remains `Mel_Gui_Handle`. The distinction is in creation and in the desktop-only operations applied to it. This keeps the handle API uniform and the type system simple.

`gui.desktop` is the first tier-2 module to be defined. Other tier-2 modules will follow the same pattern: `gui.mobile` for toasts, biometric prompts, share sheet; `gui.xr` for immersive spaces and hand tracking; `gui.web` for DOM access and history; `gui.wearable` for crown/bezel input and complications. Each tier-2 module exists only on platforms that can support it, and apps including them commit to those platforms.
