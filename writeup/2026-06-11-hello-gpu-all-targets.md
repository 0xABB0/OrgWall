# hello-gpu on android, ios, wasm — slang vendored everywhere

## Work done

Goal: un-gate hello-gpu on android (blocked on `slang` being `mel_unavailable`), then sweep every platform. Result: hello-gpu builds **and renders** on macos, android (emulator), ios (simulator), wasm (Chrome). win32 and linux blocked by pre-existing, unrelated gaps (below).

- **slang android lane.** Cross-built libslang for android-arm64 per the readme's verified recipe (NDK toolchain, generators host pass; shared `libslang-compiler.so`, STL static → self-contained). Vendored as `tools/build/vendor/slang/slang-<ver>-android-aarch64.zip` (14 MB, committed), fetched via `file://` from the repo root; reproducible via committed `build-android-arm64.sh`. android no longer `mel_unavailable`.
- **NDK r29 installed** (`~/Library/Android/sdk/ndk/29.0.14206865`). r28's clang 19.0.1 snapshot has **no C23 `#embed`**, which every hello-gpu screen uses; r29 (clang 21) supports `#embed` + `--embed-dir`. The toolchain auto-picks the highest NDK, so all android builds on this machine now use r29.
- **APK packaging** (`modules/build/package.c`): dependent third-party `prefix/lib/*.so` now travel into `jniLibs` next to `libmelody.so`.
- **gfxstream quirk** (`modules/gpu/vulkan/src/device.c`): on the emulator, `vkCmdBeginRenderingKHR` submits never complete on the host (fence never signals, zero frames latch). Bisect proved dynamic rendering is the culprit and sync2 is fine. Devices whose name contains `GFXStream` now take the render-pass floor, loudly. Verified: spinning-cube renders/animates on the emulator (host-GPU mode). Compute pipelines still fail there (`vkCreateComputePipelines VK_ERROR_UNKNOWN`, loud) — the emulator host stack (gfxstream → MoltenVK) rejects them; expected to pass on real hardware (untested, no device attached).
- **Multi-arm `mel_prebuilt`** (build framework): arms accumulate `(when, url, lib)`; the variant picks the matching arm at prepare time. Slang's desktop zips are now **target**-gated per platform×arch instead of host-`#if`-gated — cross-building linux from macos fetches the right zip.
- **`cxx` toolchain driver**: C++ TUs compile through a per-platform C++ driver (`clang++` / `zig c++` / NDK `clang++` / `em++`) via a new ninja `cxx` rule instead of `cc -x c++`, fixing missing libc++ headers under `zig cc` (linux cross).
- **win32 prebuilt extraction**: zips extract with `tar` (bsdtar) on the win32 host; `unzip` doesn't exist there. Verified `slang-compile` builds on win-pilot.
- **ios lane**: bespoke `CMAKE_SYSTEM_NAME=iOS` slang build (no upstream preset; `CMAKE_MACOSX_BUNDLE=OFF`, split-debug-info off), vendored `slang-<ver>-ios-sim-aarch64.zip` (10 MB) with a single `@rpath/libslang-compiler.dylib`. Apple packaging embeds third-party dylibs into `.app/Frameworks`; the slang-ios target rpaths `@executable_path/Frameworks`. ios un-gated. Verified hello-triangle renders on the simulator (runtime slang → MSL → Metal).
- **gui/uikit fix**: `mel_gui_set_visible(frame, true)` only pushed *screens* onto the nav stack; a frame whose children carry content directly (gpu_host's shape) never appeared — taps looked dead. Such frames now push their view controller.
- **wasm lane**: upstream `slang-wasm-libs` is single-threaded; wasm-ld refuses to link it into melody's `-pthread`/shared-memory world. Built threaded static archives (`-pthread -fwasm-exceptions -Os`, debug-stripped), vendored as `slang-<ver>-wasm-mt.zip` (14 MB) + `build-wasm-mt.sh`. Switched the gpu wasm link from `-sASYNCIFY` to `-sASYNCIFY=2` (JSPI): binaryen's ASYNCIFY=1 instrumentation OOMs (>30 GB RSS, jetsam-killed) over a binary containing all of slang, in both debug and release. Verified hello-triangle renders in headless Chrome (runtime slang → WGSL → WebGPU).

## Blocked targets (pre-existing, unrelated to slang)

- **linux**: slang now fetches+compiles (zig c++), but hello-gpu cannot link — `modules/boot` has **no linux axis** (no `main`, no event loop) and `window` is a stub. A whole platform backend is missing; separate work item.
- **win32**: slang lane proven (`slang-compile` links on win-pilot), but hello-gpu's dep closure hits the **gmp autotools** build, which fails on the box (sh-isms under cmd: `'fail' is not recognized`, `'eval' is not recognized`). Pre-existing; unrelated to this lane.

## Kludges (confessed)

- **`-sJSPI` is flagged experimental by emcc** (warning at link). It works in headless Chrome today and is the only viable path found short of removing the sync-wait usage that needs ASYNCIFY at all (a gpu-module redesign). If JSPI is unacceptable, the alternative explored and rejected was scoping ASYNCIFY with hand-maintained function lists (fragile).
- **ios artifact is simulator-only.** A device (iphoneos) libslang build does not exist; `Mel_When` cannot discriminate simulator vs device, so a device build will loud-fail at link with an arch/platform mismatch instead of a friendly message.
- **android compute screens unverified on real hardware.** Emulator host (MoltenVK via gfxstream) rejects our compute pipelines with `VK_ERROR_UNKNOWN`. The failure is loud, but the truth about real devices needs a physical device run.
- **`slang-rt`/`glslang` modules shipped on android but not ios.** Android zip carries `libslang-glslang`/`libslang-glsl-module` (dlopened lazily, SPIRV path); the ios zip carries only the compiler dylib (MSL path needs neither). Asymmetry is deliberate but undocumented outside the readme.
- **win-pilot checkout left on the work branch during the session** (restored to `main` at session close).
- **NDK r29 install is machine-global.** Every melody android build on this Mac now compiles with clang 21. Nothing pins the NDK version in-repo; a teammate on r28 will hit the `#embed` wall with no guidance from the build system.
- **emulator must run with `-gpu host`.** The default SwiftShader vulkan takes minutes per pipeline; `nob`'s emulator boot does not pass `-gpu host`, so a cold `./nob run hello-gpu android` on a fresh emulator will look hung.

## CLAUDE.md suggestions (recommendations only)

- Note under Build commands: android requires NDK r29+ (C23 `#embed`); the emulator should be booted with `-gpu host`.
- Note that `adb`/`emulator` must be on PATH for `./nob run … android` (nob shells out to bare `adb`).

## Suggestions

- Teach `nob`'s android emulator boot to pass `-gpu host` (and maybe `-no-snapshot-load`) — without it the vulkan stack is SwiftShader and pipeline creation takes minutes.
- Pin or validate the NDK version in the build system (loud error when clang lacks `#embed`, MEL-ENGINE-VIII) instead of silently picking the highest installed.
- Publish the three vendored slang zips to a hosted mirror (design doc §8.1's eventual shape) and flip the `file://` URLs; needs hosting + auth (gh is not logged in on this machine).
- Add a `simulator` axis to `Mel_When` so ios device vs simulator prebuilts can be declared separately; then add the iphoneos libslang artifact.
- Fix gmp/mpfr autotools on win-pilot (or replace with a prebuilt) — it gates every `math`-dependent target on win32, not just hello-gpu.
- Boot `modules/boot/linux` (entry + event loop) and a linux window backend; linux is otherwise slang-ready.
- gpu-scene goldens for android/ios/wasm now that those backends render (design doc §7).
