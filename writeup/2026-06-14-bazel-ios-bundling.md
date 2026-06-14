# 2026-06-14 — Bazel migration: iOS app bundling (A4) + wasm re-scoping

## Work done — what changed, and why

Continued the Bazel migration toward nob deletion. This session delivered **Track A4
(iOS bundling)** end-to-end on the macOS host, and corrected the plan's cost model for
the wasm column (Track A2) after empirical probing.

### iOS bundling — 13/16 GUI apps green, runtime-verified

The iOS GUI stack was already scaffolded in the engine (no app had exercised it): `boot`
has an `ios/` axis (`boot_apple`, programmatic `UIApplicationMain` + `MelBootAppDelegate`),
`gui` has `backend_uikit`→`gui_uikit` auto-derived from `@platforms//os:ios`, and `window`
is correctly stubbed on mobile. The only gap was app-level bundling.

- Added an `ios_application` target per GUI app (mirroring the existing `macos_application`
  shape: `bundle_id`/`bundle_name` reused, `families=[iphone,ipad]`, min-OS 14.4, shared
  `:version`, `deps=[":lib"]`), plus a per-app `ios/Info.plist` (programmatic UIKit ⇒ no
  storyboard/principal class; just `UILaunchScreen` + orientations; rules_apple injects the
  rest).
- **Verified end-to-end on `sim_arm64`**: `bazel build //apps/<app>:<app>_ios --config=ios
  --ios_multi_cpus=sim_arm64` → `.ipa` (well-formed `Payload/*.app`, ad-hoc signed, merged
  plist `iphonesimulator`/min-14.4/iphone+ipad, `arm64` binary). Booted a simulator,
  installed + launched three distinct stacks: **hello-world-gui** (full widget set renders),
  **display-gui** (live `UIScreen` enumeration renders — confirms the `display` iOS backend),
  **hello-window** (launches+alive but renders blank — desktop windowing stubbed on mobile,
  by design).
- All 13 build green together; macOS baseline intact (`process-spawn` cached PASSED;
  touched macОS apps rebuild).

The 13 green: barcode-gui, barcode-reader, camera-gui, camera-scanner, compress-lab,
display-gui, geo-tour, hello-audio, hello-speech, hello-vibration, hello-window,
hello-world-gui, melody-showcase.

### Engine fix — `process` on iOS (unblocked melody-showcase)

`modules/process/posix/src/process_backend.c` referenced
`posix_spawn_file_actions_addchdir`, which is `API_UNAVAILABLE(ios)` — a hard compile error
for any iOS app pulling `process` (melody-showcase). The Android fork path (B2) can't be
reused on iOS (`pipe2`/`execvpe` are also absent there). Fix: keep iOS on the `posix_spawn`
path, guard the `addchdir` call with `TARGET_OS_OSX`; on iOS a cwd-spawn returns `ENOTSUP`
(honest failure, not a silent ignore — MEL-CODE-007). cwd-less spawns still work. macOS path
is byte-identical (test cached).

### Deferred (genuine backend gaps — wiring removed to keep `//... --config=ios` green)

- **ballgame, hello-gpu** — `gpu`→`slang` has no iOS *runtime* artifact. Discovery: the
  vendored `tools/build/vendor/slang/slang-2026.10.2-ios-sim-aarch64.zip` (10.6 MB) already
  exists (nob lane, `build-ios-sim-arm64.sh`); the Bazel `//third-party/slang` select only
  wires macos/linux/win32. Remaining work is the **D2** dynamic-framework rule: a `slang_ios`
  repo + embedding `libslang-compiler.dylib` into `.app/Frameworks` with `@executable_path/
  Frameworks` rpath (rules_apple `apple_dynamic_framework_import`-class). Artifact present,
  rule missing.
- **music-companion** — `midi` has no iOS CoreMIDI backend (`undefined mel_midi_port_platform_
  {enumerate,open_input,close}`). `modules/midi` axes are android/linux/macos/win32 only.

### wasm re-scoping (Track A2 — corrected, not advanced)

Probed the wasm column before touching it; the recaps' "mechanical packaging rollout" framing
is wrong. Findings:
- The plain `cc_binary` is the **wrong** wasm artifact — it links `--shared-memory` against
  objects compiled without `atomics`/`bulk-memory` (proven: even the "green" hello-world-gui's
  plain binary fails identically). Only the `mel_wasm_app`→`wasm_cc_binary(threads="emscripten")`
  path is valid.
- Through the *correct* path, GUI apps fail on **genuinely missing per-module wasm backends**:
  `display` has no wasm axis at all (`undefined mel_display__enumerate`); GPU apps additionally
  need slang on wasm (the vendored `slang-...-wasm-mt.zip` exists but isn't wired into Bazel,
  and gpu's wasm link needs `-sASYNCIFY=2`). So wasm is an engine-backend effort, not wiring.
- No wasm changes were committed.

## Kludges & debt (MEL-ENGINE-VIII — confessing all)

- **iOS `process` cwd is unsupported** — returns `ENOTSUP` rather than honoring `cwd`. iOS has
  no `posix_spawn_file_actions_addchdir`; a full fork-based path (pipe + `FD_CLOEXEC` +
  `environ=envp`+`execvp`, since `pipe2`/`execvpe` are absent) would restore cwd support but is
  unwarranted — iOS sandboxing forbids subprocess spawning at runtime anyway. The build/link is
  what was needed; the runtime semantics are honest.
- **hello-window ships a blank iOS app.** It builds + launches but renders nothing (window
  module stubbed on mobile). Left wired (proves the stub path links) but it is a desktop-only
  demo with no mobile UI. Candidate for removal from the iOS set, or a mobile-appropriate
  rewrite. Flagging for a decision, not silently shipping a blank app.
- **iOS plists are near-duplicated** across 13 apps (only the display name differs). A shared
  `mel_ios_app` macro (like `mel_wasm_app`) templating the plist + `ios_application` would
  collapse this; not done (kept parity with the per-app `macos_application` style already in
  the tree).
- **BUILD files are not buildifier-clean** repo-wide (untouched files like `apps/hello-async`
  also flag). My added blocks match the existing 4-space hand style; I did **not** run
  buildifier (would churn unrelated content). A separate buildifier-normalization pass is its
  own chore.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document the host×target reality the probing exposed: **the remaining migration work is
  dominated by missing engine platform-backends, not Bazel authoring.** wasm needs per-module
  backends (display/audio/camera/…); iOS needed only bundling because its backends already
  exist. Future "rollout" tasks should be build-verified per target, never assumed mechanical.
- Note the iOS verify recipe for agents: `--config=ios --ios_multi_cpus=sim_arm64` builds for
  the simulator; `simctl create/boot/install/launch` + `simctl io screenshot` is the runtime
  gate. Simulator devices may need creating (runtimes present, devices absent by default).

## Suggestions

- **D2 (slang iOS framework embedding)** is now the single unblock for the last two iOS GPU
  apps and the artifact is already vendored — good next bounded task. Same rule shape will
  serve android/wasm slang wiring.
- **midi CoreMIDI iOS backend** (`modules/midi/apple` or `/ios`) is a self-contained engine
  task unblocking music-companion on iOS (and is the natural sibling of the existing macos
  backend).
- **wasm needs an engine-backend campaign**, not packaging: start with `display` (no axis),
  then audio/camera/etc., deciding the slang-on-wasm wiring (artifact vendored, ASYNCIFY=2
  link). Track it as engine work, not A2.
- Consider a `mel_ios_app` macro to dedupe the plist/target boilerplate before more iOS apps
  land.
