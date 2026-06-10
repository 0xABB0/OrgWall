# camera-scanner cross-target compile

Goal: make `camera-scanner` build on android, and where possible the other platforms.

## Work done

- **build framework (`modules/build/emit.c`)** — android executables now link to
  `<outdir>/<target>/libmelody.so` instead of `<outdir>/libmelody.so`. The basename is fixed
  (the gradle template hardcodes `libmelody`), so two executables in one directory
  (`paint-example` + `paint-pixmap` in `modules/paint`) generated duplicate ninja rules and
  every android emit failed at parse. Same medicine as the per-target obj dirs from the
  previous build-framework fix. Packaging is untouched: it receives the `.so` path from emit.
  `platforms.md` updated.
- **boot ios entry (`modules/boot/src/ios/entry.m`)** — the owed `UIApplicationMain`-subordinate
  entry on the guest waiter, a direct port of the web entry's shape: GCD embedder
  (`dispatch_async` on the main queue / `dispatch_after`; negative delay = wake only on ring),
  one `mel_vat_step` per host callback, redrive when the waiter wasn't reached, exit hooks +
  `exit(code)` on retention loss. Vat and UIKit both live on the main thread, matching the
  uikit gui backend's expectations.
- **boot ios lifecycle (`modules/boot/src/ios/lifecycle.m`)** — `UIApplication` notifications
  (active/resign/background/foreground/terminate/memory-warning) forwarded to `mel_app__emit`;
  mirror of `src/macos/lifecycle.m`.
- **`modules/boot/build.c`** — ios sources + UIKit link; readme Entries/Owed updated.

## Verified

- android: ninja emit parses, full build, APK packaged.
- ios: builds and packages `camera-scanner.app` (ios-sim-debug).
- macos: builds and packages.
- wasm: builds, links `camera-scanner.html`.
- `./nob test` on host: 74/77 pass; the 3 failures (`gpu-resources`, `gpu-metal`, `gpu-scene`)
  reproduce identically on the untouched main checkout — pre-existing, not from this wave.

## Not done — linux, win32

Both are blocked on missing framework subsystems, not on compile fixes:

- linux link fails on: `main` (no boot linux entry), every `mel_painter_*`/`mel_canvas_create_opt`/
  `mel_panel_create_opt` (paint has no linux backend; gui xcb lacks canvas/panel widgets), and
  vat has no epoll waiter (`waiter_ui` is cocoa-only).
- win32 would fail the same way on `main` (boot has no win32 entry; vat has no IOCP waiter), so
  the remote-box roundtrip was not spent. paint/gdi and gui/winui do exist, so win32 is closer
  than linux: it needs only the boot entry + waiter.

All of these are already confessed as owed in `modules/boot/readme.md` and `modules/vat/readme.md`.

## Kludges

- The ios entry calls `exit(code)` when the vat dies. iOS apps are not supposed to self-exit;
  this is web-entry parity (`emscripten_force_exit`) and honest for retention loss, but a
  store-facing app would want suspension semantics instead. Sanctioned-by-shape, still debt.
- The ios entry is compile/link-verified only; no simulator run this wave (noted in boot's
  readme Owed section).
- `argc`/`argv` are stashed in globals before `UIApplicationMain` since the delegate has no
  argument channel. Standard UIKit shape, but globals nonetheless.

## CLAUDE.md suggestions

- Worktrees lack the `nob` binary (gitignored); the bootstrap line
  `clang -std=c23 -g -Imodules/build -o nob nob.c` could be documented in the Build section.

## Suggestions

- gpu test targets crash on this host (`gpu-resources` etc., `CRASH res_sampler.create_destroy_churn`)
  even on main; worth a look or an explicit skip-when-headless.
- win32 is one boot entry + one vat waiter away from camera-scanner coverage; linux additionally
  needs a paint backend and xcb canvas/panel widgets.
