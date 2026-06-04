# app

The application spine: a uniform program entry across every platform, a refcounted
subsystem, and the host's app-lifecycle phases delivered through Melody's event substrate.

## Why it exists

Every platform starts a program differently — `main`, `WinMain`, `UIApplicationMain`, a JNI
`nativeStart`, an Emscripten `main` attached to the browser loop. `app` owns that divergence
behind one shim so the framework user writes a single `mel_app_setup(Mel_Reactor*)` and never
touches a platform entry point (MEL-ENGINE-II). The shim brackets the reactor with subsystem
init/quit, so lifecycle is wired before `mel_app_setup` runs and torn down after the loop ends.

The host signals an application's coarse phases — it will terminate, it is low on memory, it
resigned or became active, it entered the background or will re-enter the foreground. `app`
surfaces those uniformly as a flag-tagged `Mel_App_Lifecycle_Event` over the same event channel
the rest of the framework uses (display, input), so the user joins lifecycle to anything else
without special coupling (MEL-ENGINE-IX).

## Public surface

- `app/app.h` — umbrella; declares the user-provided `mel_app_setup`, re-exports the rest.
- `app/subsystem.h` — `mel_app_init` / `mel_app_quit` refcounted bring-up of the lifecycle
  channel and platform provider; `mel_app_refcount` / `mel_app_initialized`. The first init
  starts the platform provider; the matching last quit stops it. Underflowing quit asserts
  (MEL-ENGINE-VIII).
- `app/lifecycle.h` — `Mel_App_Status` bitset (severity mask + condition flags, inline
  predicates, no error strings); the `MEL_APP_PHASE_*` flag bitset (anonymous, not an enum —
  MEL-CODE-001); `Mel_App_Lifecycle_Event` (phase bits + monotonic timestamp);
  `mel_app_lifecycle_poll` (pull) and `mel_app_lifecycle_subscribe` / `_unsubscribe` (push,
  delivered on a caller-supplied `Mel_Executor*`); `mel_app_active` / `mel_app_foreground`
  current-state accessors.
- `app/provider.h` — the backend contract. A platform TU implements
  `mel_app__register_platform_provider`, registering a `Mel_App_Provider_Desc` (named, with
  `start`/`stop`) and pumping native hooks into the core via `mel_app__emit(phase)`.
  `mel_app__reactor` exposes the loop reactor to a backend that needs to marshal a foreign-thread
  signal back onto the loop.

The phases are a bitset, not an enum: a backend ORs the phases it can observe and the user reads
the bits it cares about. `mel_app__emit` updates the active/foreground state from the phase bits
before firing, and asserts loop-thread affinity when a reactor was bound at init.

## Backends

- macos — `NSApplication` notifications (`WillTerminate`, `DidBecomeActive`, `WillResignActive`,
  `DidHide` → background, `WillUnhide` → foreground) via `NSNotificationCenter`.
- ios — `UIApplication` notifications, including `DidReceiveMemoryWarning` → low-memory and the
  full background/foreground pair.
- linux — `SIGTERM` / `SIGINT` caught by a handler that writes a self-pipe; a reactor source polls
  the read end and emits `WILL_TERMINATE` on the loop thread (async-signal-safe handoff).
- win32 — `SetConsoleCtrlHandler`; the handler runs on a foreign thread and marshals
  `WILL_TERMINATE` onto the loop via `mel_reactor_post`.
- wasm — Emscripten `visibilitychange` (→ resign/background and foreground/active) and
  `beforeunload` (→ will-terminate).
- android — Activity lifecycle forwarded through JNI (`nativeOnResume` / `OnPause` / `OnStop` /
  `OnDestroy` / `OnLowMemory`) directly into `mel_app__emit` on the UI/loop thread.

## Dependencies

`core`, `gui`, `reactor`, `allocator`, `collection`, `event`, `executor`, `thread`, `time`,
`log`, `platform`.
