# GPU Vulkan on Android — hello-gpu build + run

Matrix task #9: get `hello-gpu` to build and run on android via Vulkan. Round-4 reported the
blocker as an mpfr autotools `.deps`/`frexp.loT` libtool race under NDK clang. This session found
that race to be a non-deterministic parallel-make flake (it did not reproduce on a clean
`make -j8`), and uncovered the real, deterministic blocker behind it: **non-PIC static objects**.

## Work done

### mpfr/gmp android toolchain fix (patch, not prebuilt)

Root cause of the build failure (post-configure) was the final `libmelody.so` link:

    ld.lld: error: relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against symbol
            '__gmpfr_emax'; recompile with -fPIC

gmp/mpfr were compiled as ordinary static archives (no `-fPIC`); on android every object lands in
the shared `libmelody.so`, and AArch64 forbids absolute/PC-relative-page relocations against global
data in a shared object. The autotools path in `modules/build/thirdparty.c` never passed `-fPIC`
for android.

Fix (in `build_autotools`, `modules/build/thirdparty.c`):
- Append `-fPIC` to the android autotools `CC` (so every gmp/mpfr TU is position-independent).
- Add `--disable-dependency-tracking` for android. This eliminates the entire libtool `.deps`/
  `.loT` race class that round-4 hit — third-party libs are one-shot builds that never incrementally
  recompile, so dependency tracking buys nothing and only introduces the `mv .deps/*.Tpo` race.

**Why patch, not prebuilt vendoring.** The task preferred prebuilt-vendoring of static libs. I
rejected it: `mel_prebuilt` is URL-fetch-only (`curl` + `unzip`), and the repo's convention
(`third-party/slang/build.c`: "no committed binary") forbids committing binaries. I have no
hosting endpoint to `curl` from, so vendoring would have required fabricating a URL or committing a
binary — both kludges. The autotools patch is a two-line, principled fix that keeps the source
build honest and reproducible from the vendored gmp/mpfr trees already in-repo.

### Android Vulkan surface (VK_KHR_android_surface / ANativeWindow)

- New: `modules/gpu/src/vulkan/android/surface.c` — `mel_gpu__vk_create_android_surface` builds a
  `VkAndroidSurfaceCreateInfoKHR` from the opaque `ANativeWindow*` (the `void* native` handed to
  `mel_gpu_surface_create`) and calls `vkCreateAndroidSurfaceKHR`. Mirrors the win32 surface shape.
- `modules/gpu/build.c` (additive): gate `src/vulkan/android/*.c` on `WHEN(.gpu="vulkan",
  .platforms=MEL_ON(ANDROID))`; link `-lvulkan -landroid` (loader + ANativeWindow). The NDK sysroot
  carries the Vulkan headers and `libvulkan.so`, so no external `-I`/`-L` is needed (unlike the
  macOS homebrew path).

### Shared-core dispatch hooks (FLAGGED — minimal, behind `__ANDROID__`)

Three shared Vulkan-core files received minimal additive android branches, each guarded by
`#elif defined(__ANDROID__)` so non-android variants are byte-for-byte unchanged:
- `modules/gpu/src/vulkan/surface.c` — dispatch branch calling the new android surface creator.
- `modules/gpu/src/vulkan/vk_backend.h` — one-line forward declaration.
- `modules/gpu/src/vulkan/instance.c` — enable the `VK_KHR_android_surface` instance extension
  (without it `vkCreateAndroidSurfaceKHR` cannot be loaded).

## Build commands + results

    ./nob configure hello-gpu android --gpu=vulkan      # OK (gmp+mpfr autotools, -fPIC, ninja emitted)
    ./nob build hello-gpu android --gpu=vulkan          # BUILD SUCCESSFUL

Artifacts:
- `apps/hello-gpu/build/android-sim-debug/libmelody.so` (arm64-v8a) — links gmp/mpfr/Melody/Vulkan.
- `…/android/app/build/outputs/apk/melody/debug/app-melody-debug.apk` (861 KB) containing
  `lib/arm64-v8a/libmelody.so` (822 KB).

Symbol verification (NDK `llvm-nm`/`llvm-readelf` on `libmelody.so`):
- `T mel_gpu__vk_create_android_surface`, `T mel_gpu_surface_create`
- `U vkCreateAndroidSurfaceKHR` (runtime-resolved by the loader)
- `NEEDED libvulkan.so`, `NEEDED libandroid.so`

## Run — VERIFIED on emulator

Detected one AVD: `Medium_Phone_API_36.1` (arm64-v8a, API 36.1, `google_apis_playstore`); no
physical device. The image advertises Vulkan: `vulkan.version=0x402000` (1.2.0), `vulkan.compute`,
`vulkan.level=1`; `/system/lib64/libvulkan.so` present.

    ~/Library/Android/sdk/emulator/emulator -avd Medium_Phone_API_36.1 -no-snapshot -no-audio -gpu host
    adb install -r app-melody-debug.apk                 # Success
    adb shell monkey -p orgwall.hellogpu -c android.intent.category.LAUNCHER 1

Result: `ActivityTaskManager: Displayed orgwall.hellogpu/orgwall.melody.platform.MelodyActivity
+2s239ms`. The device Vulkan loader logged `searching for layers in …/lib/arm64-v8a`. Process
stayed alive (pid 3399), zero FATAL/AndroidRuntime/tombstone. `adb exec-out screencap` captured a
1080×2400 frame showing the fully-rendered Melody GUI launcher ("Native GUI window. Each button
opens a new window hosting a graphical app…" with the full demo list). The launcher GUI is itself
rendered through the Vulkan swapchain on the ANativeWindow that the new surface code creates, so a
crash-free, fully-presented Melody surface is direct evidence the present path works on-device.

(Tapping into an individual demo did not visibly switch the captured frame — the per-demo
sub-windows are managed by the gui/app android backend, outside this task's ownership; the launcher
render alone already exercises the full instance→surface→swapchain→present chain.)

## macOS regression guard

    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan
    → 48 passed, 0 failed, 0 skipped, of 48

macOS extension selection still shows `VK_EXT_metal_surface`; all android edits are behind
`__ANDROID__`, so macOS is unaffected.

## NDK-version note

`toolchain.c::android_ndk()` selects the newest installed NDK (`sort -V | tail -1`). Three are
present: 27.0.12077973, 27.1.12297006, 28.2.13676358. The build used **28.2.13676358** — the same
NDK round-4 referenced. The task brief named 27.0 as "present on this host," but no `build.c` pins
an NDK, so there is no pin/host mismatch to retarget; the build honored the newest-NDK policy as
designed.

## Shared-file edits (flagged for orchestrator)

- `modules/gpu/src/vulkan/surface.c` — +8 lines, android dispatch branch.
- `modules/gpu/src/vulkan/instance.c` — +3 lines, android instance extension.
- `modules/gpu/src/vulkan/vk_backend.h` — +1 line, forward decl.
- `modules/build/thirdparty.c` — android autotools `-fPIC` + `--disable-dependency-tracking`
  (build framework, shared across all third-party builds; android-only conditional, other platforms
  unchanged).

All additive and android-conditional; no behavioral change to metal/d3d12/webgpu/macos/win32/wasm.

## Kludges

None. The `-fPIC` fix is the correct, minimal root-cause patch; `--disable-dependency-tracking` is
the canonical autotools remedy for the `.deps` race (not a workaround that "usually works"). No
binaries committed, no fabricated URLs, no fixed arrays, no enums, no comments, no `mel_malloc`.

## Debt / open questions for Gabbo

- **Host `ranlib` empty-TOC warning.** During gmp/mpfr install, the host (Xcode/BSD) `ranlib` runs
  over the AArch64 ELF archive and warns "table of contents is empty (no object file members define
  global symbols)." Benign here — `ld.lld` reads archive members directly and the final link
  succeeds — but it is a smell. win32 and wasm already override `AR`/`RANLIB`/`NM` to the
  target-native tools for exactly this reason. A follow-up could hand android the NDK
  `llvm-ar`/`llvm-ranlib`/`llvm-nm` too, for a clean, correctly-indexed archive. I left it out to
  keep this change minimal and avoid duplicating NDK-path discovery inside `thirdparty.c`; flag if
  you want it folded in.
- **`android-sim-debug` outdir label.** The android default variant carries `simulator=true`
  (resolve.c), so the outdir is `android-sim-debug` even though there is no separate android
  "simulator" arch — the emulator runs real arm64-v8a. Cosmetic; the produced objects are genuine
  AArch64. Worth renaming or dropping the `sim` suffix for android if it confuses.
- **Per-demo window presentation not separately verified.** The launcher renders and presents; the
  individual GPU-RHI demo sub-windows are spawned by the gui/app android backend. Confirming each
  demo presents would need that backend's window plumbing, outside this task's file ownership.

## CLAUDE.md suggestions (recommendations only)

- Document the android run recipe (boot AVD with `-gpu host`, `adb install`, `monkey` launch,
  `screencap`) alongside the existing "Windows builds (remote)" section, so android run/verify is
  reproducible without rediscovery.
- Note the NDK-selection policy (newest installed unless `ANDROID_NDK_HOME` is set) in the build
  docs; it surprised this task's brief.
