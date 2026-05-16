# Cross-Platform GUI Top-Level — Stage

## Problem Statement

The current GUI library treats "window" as a uniform concept and exposes `mel_gui_create_window` to applications. This breaks on every platform that is not Windows-shaped. On macOS, iOS, Android, Vision OS, Quest, web, and wearables, the OS hands the app a root surface — it does not let the app create top-level windows freely. Calling something a "window" in the public API lies about the platform's semantics on most targets.

There is also the inverse problem: even though "window" is wrong for the cross-platform API, desktop platforms genuinely have powerful window machinery — transparency, framelessness, click-through, custom z-levels, multi-monitor positioning — that I want to use for desktop widgets and for visual effects like characters animating outside a window's bounds. Hiding those behind a least-common-denominator API kills the use case; exposing them through the cross-platform API forces every other platform to implement no-ops or fakes.

The existing "activity" first-class concept (carried over from Android) is also wrong: it is just a string switch over a single global builder function, no object, no state, no lifecycle. It exists only because the Android JNI shim needed to dispatch a name into native code. It does not earn its keep on any other platform and pollutes the cross-platform vocabulary.

## Solution

The portable top-level concept is the **Stage**. A `Mel_Gui_Handle` of stage shape sits at the root of every UI tree. Apps never call "create window" in cross-platform code; they ask the framework for the default stage of a reactor, or — on platforms where additional stages are supported — they create more.

The application entry point is `mel_main(Mel_Reactor*, int argc, char** argv)`. The framework provides the real `main` / `WinMain` / Android JNI shim / web entry / etc., constructs the system reactor, and calls `mel_main` on it. Inside `mel_main`, the user retrieves the default stage and builds the UI under it.

A stage maps per-platform:

- Win32 / X11 / Wayland / Cocoa-desktop: an OS top-level window
- iOS / iPadOS: the root view of the existing UIWindow
- Android: the activity's content FrameLayout
- Vision OS: the default WindowGroup's root view
- Quest / native OpenXR: a swapchain-backed compositor layer; widgets are viewport-only there
- Web: a canvas or container element under document
- watchOS / Wear OS: the watch face surface; control set is limited to platform primitives

Multiple stages per reactor are allowed where the platform supports it. On single-stage platforms, attempting to create a second stage returns NULL with `MEL_GUI_ERR_PLATFORM_LIMIT`. Applications that need a second window can either accept the failure on those platforms or create a second reactor (if multi-reactor is supported on that platform).

The "activity" concept is removed entirely. Activity-style navigation (screen-to-screen replacement) is implemented in user code by destroying the current stage's children and building new ones. The library has no opinion on app navigation.

## Implementation Decisions

- The Stage is a `Mel_Gui_Handle` with a stage-shape backing. It is not a separate type; the handle is uniform.
- `mel_gui_default_stage(Mel_Reactor*)` returns the framework-provided stage created at reactor startup. Every reactor that supports a UI has exactly one default stage.
- `mel_gui_create_stage(Mel_Reactor*, const Mel_Gui_Stage_Opt*)` creates additional stages on supporting platforms. Returns NULL on unsupported platforms.
- Stage creation accepts capability flags as a best-effort hint: `OPAQUE`, `TRANSPARENT`, `FRAMELESS`, `TOPMOST`, `NON_RESIZABLE`. Platforms apply what they can, ignore what they cannot. `mel_gui_stage_caps(stage)` returns a bitmask of capabilities that actually took.
- All handle-level operations (set_window_pos, set_text for title, invalidate, request_frames) work on a stage exactly as on any other handle. There is no separate Stage API surface.
- The stage handle has no parent. Children attach to it like to any other handle.
- Lifecycle events (foreground/background, suspend/resume, app destroy) flow to the stage as typed callbacks on the stage's `_opt` callback structs, not through a global broadcast.
- The string-switch `mel_gui_app_build_activity` and `mel_gui_app_start_activity` are removed. The platform bootstrap layer calls `mel_main(reactor, argc, argv)` directly.
- `MEL_GUI_MSG_APP_CREATE`, `MEL_GUI_MSG_APP_RESUME`, `MEL_GUI_MSG_APP_PAUSE`, `MEL_GUI_MSG_APP_DESTROY`, `MEL_GUI_MSG_APP_BACK`, `MEL_GUI_MSG_APP_CONFIG_CHANGED` are removed (the entire message bus is removed in PRD 06, but the broadcast model is also conceptually superseded here).

## Testing Decisions

A good test of the Stage abstraction verifies external behavior: that `mel_gui_default_stage` returns a valid handle on a supported reactor, that `mel_gui_create_stage` succeeds on desktop platforms and returns NULL elsewhere with the correct error code, that capability flags applied to a stage are reflected in `mel_gui_stage_caps`, that stage children are destroyed when the stage is destroyed.

Modules under test: `gui/` (stage primitive), per-platform `gui.platform.*` (creation + capability mapping).

Prior art: the current `mel_gui_create_window` tests, if any, are in app code (`hello-world-gui`). No isolated GUI tests exist in the repo. New tests for stage creation can run on host with a stubbed platform layer.

## Out of Scope

- The Mel_Gui_Desktop_Window with full window-magic powers — that is its own PRD (04).
- Lifecycle event semantics that are platform-specific (Android `onConfigurationChanged`, macOS `applicationShouldTerminate` and so on). Those flow through per-platform tier-2 modules, not through the Stage.
- Multi-stage layout on platforms that allow it (e.g., split-screen panes on Android 12+). Each stage is independent; cross-stage coordination is application logic.
- Migration from existing apps that use `mel_gui_create_window` and the activity mechanism. The PRD documents the target shape; migration is implementation work.

## Further Notes

The name "Stage" is taken from JavaFX, where it serves the identical role: the platform-provided top-level container into which a scene is mounted. The metaphor (theater stage) is intuitive in GUI context. "Tableau" was considered as a more archaic alternative; "Stage" wins on established CS precedent.

The cross-platform API never uses the word "window." The word survives only in `gui.desktop`'s internal vocabulary (where the platform actually has windows) and in per-platform implementation files.
