# boot

The vat-era program entry: the framework owns the platform `main`, the application owns
`mel_app_setup(Mel_Vat* root)`. Setup registers concerns — opens sources, windows, retains,
posts work — and returns; it never blocks and never runs the loop. This is the successor to
the entry half of `app` (which stays reactor-bound for the gui apps until they migrate);
nothing in `boot` touches `app`.

## Why it exists

On iOS and the web the host owns the loop: a blocking `main` is impossible, so an application
that defines `main` is wrong on at least two platforms (MEL-ENGINE-VII). `boot` owns the
divergence — a sovereign entry runs the root vat's driver to completion; a subordinate entry
returns from `main` with a live runtime and steps the vat from host callbacks. The application
code is identical across both (MEL-ENGINE-II): a setup that registers nothing yields a run
that returns immediately — retention-based exit is the CLI-app story, no separate path.

## Surface

- `boot/boot.h` — declares the user-provided `void mel_app_setup(Mel_Vat* root)`, plus
  `mel_app_argc()` / `mel_app_argv()` (stored before setup runs), `mel_app_set_exit_code(int)`
  (the process exit code after the loop ends), and `mel_app_on_exit(fn, user)` — teardown
  hooks in a dynamic array, fired LIFO after the run ends and before the vat closes.

## Entries

- macos (`src/macos/entry.c`) — sovereign. `main` opens the root vat over
  `mel_vat_waiter_ui` + `mel_vat_driver_fair(alloc, 64)` on the heap allocator, calls
  `mel_app_setup`, `mel_vat_run`, fires the exit hooks, tears down, returns the exit code.
- ios (`src/ios/entry.m`) — subordinate. `main` hands the thread to `UIApplicationMain`; the
  delegate's `didFinishLaunching` opens the root vat over `mel_vat_waiter_guest` with a GCD
  embedder (`schedule_work` → `dispatch_async` on the main queue; `schedule_delayed_work` →
  `dispatch_after`, negative delay = wake only on ring), calls `mel_app_setup`, and drives one
  `mel_vat_step` per host callback. A turn that never reached the waiter is redriven with an
  immediate `dispatch_async`; quit or retention loss fires the exit hooks and `exit`s with the
  stored code. `src/ios/lifecycle.m` forwards the `UIApplication` notifications into
  `mel_app__emit`.
- web (`src/web/entry.c`) — subordinate. `main` opens the root vat over
  `mel_vat_waiter_guest` with an emscripten embedder (`schedule_work` → `emscripten_async_call`
  / proxied to the main runtime thread; `schedule_delayed_work` → `emscripten_set_timeout`,
  negative delay = wake only on ring), calls `mel_app_setup`, drives one `mel_vat_step` per
  host callback, and returns from `main` via `emscripten_exit_with_live_runtime`. A turn that
  never reached the waiter (ready work ran under budget) is redriven with an immediate
  callback; quit or retention loss fires the exit hooks and `emscripten_force_exit`s with the
  stored code.

## Owed (MEL-ENGINE-VIII)

- linux / win32 / android entries.
- The ios entry was verified to compile and link only; an app was not run on the simulator
  this wave.
- The web entry never cancels stale `emscripten_set_timeout` arms; a superseded deadline
  costs one empty drive when the old timer fires.
- The web entry was authored against the external probe's guest pump; the wasm build of a
  full app was not verified end-to-end this wave.

Deps: core, allocator, collection, vat.
