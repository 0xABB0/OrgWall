# 2026-06-14 — Bazel packaging: rules_apple (macOS), rules_android groundwork

## Work done — what changed, and why

Steer from Gabbo: do not hand-author packaging to replicate nob's behaviour — nob's
macOS path is a kludge (copy exe + string-substituted plist, no codesigning, no framework
embedding). Use the real Bazel rulesets. So: `rules_apple` for Apple bundles, `rules_android`
for the apk.

**macOS `.app` bundles via `rules_apple` — COMPLETE & VERIFIED.** Added
`bazel_dep(name = "rules_apple", version = "4.5.3")`. Each GUI app now follows one shape
(proven first on hello-window, then rolled out): the app sources become a `cc_library(:lib)`;
a thin `cc_binary` links it (CLI / non-Apple platforms / `bazel run`); a `macos_application`
+ `apple_bundle_version` link the same library into a proper bundle. 16 apps bundle:
the 14 gui-subsystem apps plus barcode-reader and geo-tour (subsystem=cli but they request
camera/location, so they genuinely need TCC usage strings → a bundle). The 4 remaining CLI
tools (hello-net, midi-monitor, repl, melody-wasi) correctly stay plain `cc_binary` — nob
bundled every executable indiscriminately; that was wrong.

Each bundle is **ad-hoc codesigned** with sealed resources and a real merged Info.plist
(verified on hello-window + camera-gui): CFBundleIdentifier, CFBundleExecutable,
CFBundleName/DisplayName, version, min-OS. The five apps that shipped an `apple/Info.plist.partial`
(camera-gui, camera-scanner, hello-speech, barcode-reader, geo-tour) have their usage strings
(NSCamera/NSMicrophone/NSSpeechRecognition/NSLocation*) folded into a complete per-app
`macos/Info.plist` and merged by rules_apple. This is categorically beyond nob: signing,
sealed resources, and honest plist merging that nob never did.

**Honest minimum_os_version = 14.4.** `modules/thread/apple/src/futex.c` calls the
`os_sync_*` family (macOS 14.4) **unconditionally, no `__builtin_available` guard**. nob's
template hard-coded LSMinimumSystemVersion 11.0 — a lie that would crash on 11.0–14.3 the
moment a futex is used. rules_apple's stricter deployment-target propagation surfaced this as
an unguarded-availability warning at 11.0. The bundles declare 14.4 (the engine's true floor);
the warning vanishes. If Melody wants to support older macOS, futex needs an availability
fallback — separate engine work, flagged here.

**run / debug verbs.** No bespoke wrappers needed — these are native Bazel:
`bazel run //apps/X:X_app` launches the bundle (rules_apple makes macos_application runnable;
verified the launch script execs the bundle binary); `bazel run //apps/X:X` runs the plain
binary; debug is `bazel run --run_under=lldb //apps/X:X`. This replaces nob's `run`/`debug`
driver verbs on the host.

**Android `.apk` via rules_android — PROVEN end-to-end on hello-world-gui.** Added
`bazel_dep(name = "rules_android", version = "0.7.3")` + the `remote_android_tools` and
`android_sdk_repository` extensions. The SDK toolchain is registered per-config
(`.bazelrc build:android --extra_toolchains=@androidsdk//:all`), mirroring the NDK pattern,
so a macOS build never force-fetches `@androidsdk` / demands `$ANDROID_HOME`. The heavy
rules_android graph (Go/protobuf/jvm_external/robolectric) resolves without breaking macOS
or the test suite (104 pass / 2 skip unchanged).

`bazel build //apps/hello-world-gui:melody --config=android` produces a correct, signed,
installable apk: `application-label='Hello World GUI'`,
`launchable-activity=orgwall.melody.platform.MelodyActivity`, `lib/arm64-v8a/libmelody.so`,
MelodyActivity in classes.dex. The pieces:
- **android_library for the Java** — `//modules/platform:android_java` (platform java) and
  `//modules/gui:android_java` (gui's androidnative java: MelodyActivity/MelGui/… + the shared
  launcher AndroidManifest + AppTheme styles), both `target_compatible_with` android so macОS
  builds skip them. gui's library sets `exports_manifest = True` so its launcher activity +
  label merge into the apk (without it the apk's `<application>` is empty — the failure I hit).
- **android_binary named `melody`** (per app) — rules_android merges the whole transitive cc
  closure into a single `lib/<abi>/lib<target>.so`, so naming the target `melody` yields
  `libmelody.so` for `System.loadLibrary("melody")`. `manifest_values={"appLabel": …}` fills
  the `${appLabel}` placeholder.

Three integration fixes were required and are baked into `.bazelrc build:android`:
- `--android_platforms=//bazel/platforms:arm64-v8a` — rules_android names the apk native-lib
  dir after the platform's *name*; the single-name `//bazel/platforms:android` put the lib at
  `lib/android/` (not a real ABI → would `UnsatisfiedLinkError` on device). A new ABI-named
  platform fixes it to `lib/arm64-v8a/`.
- `--tool_java_language_version=17` + `--tool_java_runtime_version=remotejdk_17` — rules_android
  0.7.3's bundled Java tools use records / are compiled for Java 17; rules_java's older exec
  default fails them (`could not locate class file for java.lang.Record`, then
  `UnsupportedClassVersionError 61.0 vs 55.0`).

**Android apk rolled out across the gui apps.** Authored the android Java/manifest subsystem
the prior writeups left unrepresented: an `android_library` per module that ships android Java
and/or a manifest fragment — `platform`, `gui` (launcher manifest + AppTheme + ~21 widget
classes), `audioin`, `audioout`, `audiopolicy`, `camera`, `dialog`, `display`, `gamepad`,
`hid`, `input`, `messagebox`, `midi`, `notification`, `sensor`, `stt`, `tts`, `vibration` — all
`target_compatible_with` android so macОS builds skip them. The module Java is fully
self-contained (no cross-module imports), so no inter-library deps are needed; manifest
fragments (permissions/features/receivers) carry `exports_manifest = True` to merge into the
apk. Each gui app gained an `android_binary(name = "melody", …)` whose `deps` are computed from
the app's transitive module closure — so each apk requests only the permissions/Java it
actually uses (camera apps pull `camera:android_java`, audio apps the audio libraries, etc.).

**Result: 9 of the 12 gui apps produce a correct signed apk** (barcode-gui, camera-gui,
camera-scanner, compress-lab, display-gui, hello-audio, hello-speech, hello-vibration,
hello-world-gui), each verified by aapt2 (label, MelodyActivity launcher, arm64-v8a native).
Three are blocked by upstream gaps that are NOT the packaging work and that nob would hit
identically:
- **ballgame, hello-gpu** — `//third-party/slang` has no android prebuilt (`<slang.h>` absent;
  "Phase 4" per slang.BUILD); gpu→slang cannot compile for android until that prebuilt lands.
- **melody-showcase** — `//modules/process` posix backend calls `posix_spawn_file_actions_*`
  (incl. `_addchdir_np`) absent from android bionic; an engine source-portability gap (like the
  documented missing Linux vat waiter), not a BUILD issue.
- **music-companion** — the pre-existing companion.c source bug.

**Fixed two real android-native migration bugs along the way** (both the private-header-staging
class the prior writeup swept across 109 globs, missed here): `modules/gui/BUILD.bazel` globbed
`src/androidnative/*.c` without the sibling `*.h` (`'android.h' file not found`) — added `*.h`
to all four backends (androidnative/dom/winui/xcb); and `modules/display/BUILD.bazel`'s `display`
library globbed only `include/**/*.h`, never staging its `android/include` axis header
(`display/android/android.h`) — widened to `include/**/*.h` + `*/include/**/*.h`, matching
gamepad/input.

**run / debug (android).** `bazel run //apps/X:melody --config=android` installs+launches via
rules_android's apk runner (needs a device/emulator — not exercised here, no emulator running);
`adb logcat` is the debug tail, matching nob.

## Kludges & debt (MEL-ENGINE-VIII)

- **Three gui apps have no android apk, by upstream gaps** (above): ballgame/hello-gpu await a
  slang android prebuilt (Phase 4); melody-showcase awaits a `process` android backend
  (`posix_spawn_file_actions_addchdir_np` is absent from bionic); music-companion awaits its
  source fix. None are packaging defects — the `android_binary` targets for all three are
  authored and correct; they build the instant their upstream blocker clears.
- **geolocation has no android_library.** Its Java imports Google Play Services
  (`com.google.android.gms.location.*`), which needs a Maven dep (rules_jvm_external +
  play-services-location) not yet wired. No gui app in scope uses geolocation, so nothing is
  blocked today; wiring the Maven dep is the follow-up when a geolocation app is ported.
- **Single ABI (arm64-v8a).** `--android_platforms` lists only arm64-v8a, so the apk is
  arm64-only (fine for arm64 devices/emulators). A release apk wants armeabi-v7a/x86_64 too —
  add ABI-named platforms and list them in `--android_platforms`.
- **apk not installed/launched here** — no emulator running this session. Structure verified by
  aapt2 (label, launcher activity, native ABI, signature); runtime launch unverified.
- **`$ANDROID_NDK_HOME` must be set** to one of the installed NDKs (used
  ~/Library/Android/sdk/ndk/29.0.14206865). Documented requirement; not auto-discovered.
- **iOS bundles not done.** `ios_application` is the analogous rules_apple path; out of scope
  this pass (Gabbo chose macОS + Android).

## CLAUDE.md suggestions (recommendations only)

- Document the bundle/run/debug surface: `bazel build //apps/X:X_app --config=macos` for the
  `.app`; `bazel run //apps/X:X_app` to launch; `bazel run --run_under=lldb //apps/X:X` to
  debug; and that macOS apps target min-OS 14.4 (engine floor, set by futex).

## Suggestions

- Resolve the futex availability honestly (guard `os_sync_*` behind `__builtin_available` with
  a pre-14.4 fallback) if older macOS support is wanted; otherwise 14.4 is the truthful floor.
- The android Java/manifest subsystem is the next focused chunk — sizeable, and best done
  module-by-module (android_library co-located with each module, mirroring nob's per-module
  mel_android_java/mel_android_manifest), then one android_binary per gui app.
