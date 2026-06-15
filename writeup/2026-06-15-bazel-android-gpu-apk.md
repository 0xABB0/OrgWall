# 2026-06-15 — Bazel migration: android GPU apk (Vulkan 1.1/1.2 proc-addr dispatch)

Picked the **android GPU apk** frontier (of the three open: win32-link, android-GPU, wasm-GPU).
The blocker named in the prior recap — `gpu/vulkan` direct-linking Vulkan 1.1/1.2 entrypoints
android's API-26 `libvulkan.so` doesn't export — is now closed. Both GPU apks build, link, and
run on the emulator.

## Diagnosis

Android's NDK `libvulkan.so` **link stub** at `minSdk=26` exports only the core Vulkan 1.0
symbol set plus `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`. The native link uses
`--no-undefined`, so any *directly-called* 1.1/1.2 core entrypoint is an undefined symbol at
apk link time — independent of the (newer) loader the device actually ships. The WSI `*KHR`
extensions (swapchain/surface/android_surface) **are** in the stub, so they linked fine; the
backend already proc-addr'd the 1.3 dynamic-rendering / sync2 commands. That left exactly four
offenders, six call sites (full enumeration of every `vk*` call in the backend confirmed no
others):

- `vkGetPhysicalDeviceProperties2` — `caps.c:52`, `caps.c:148`
- `vkGetPhysicalDeviceFeatures2` — `caps.c:70`, `device.c:150`
- `vkGetPhysicalDeviceMemoryProperties2` — `memory.c:223`
- `vkGetBufferDeviceAddress` — `buffer.c:295`

`mini-gmp`-style shortcuts don't apply here; the fix is the backend's own established
dispatch convention, not a new mechanism (MEL-ENGINE-IX). **volk was considered and rejected**:
the engine creates multiple devices, and volk's global function-pointer table loads from the
last device — a real multi-device hazard; per-device volk tables would mean rewriting all ~25
TUs to `table->vkFoo`, a far larger surface than the four entrypoints actually used.

## What landed (commit `95334975`)

Three of the four are physical-device queries → an **instance dispatch table** on
`Mel_Gpu_Instance` (`get_physical_device_{properties2,features2,memory_properties2}`), loaded
once via `vkGetInstanceProcAddr` right after `vkCreateInstance`. `mel_gpu__caps_probe` gained an
`inst` parameter to reach it; `device.c`/`memory.c` reach it via the `inst` they already hold
(`dev->instance`).

The fourth is device-level → a **`get_buffer_device_address` PFN** on `Mel_Gpu_Device`, loaded
via `vkGetDeviceProcAddr` after `vkCreateDevice`, gated on the BDA feature — mirroring the
existing `cmd_begin_rendering`/`cmd_pipeline_barrier2` PFNs exactly.

**Fail with honor (MEL-ENGINE-VIII):** instance creation logs and returns NULL if any required
core-1.1 query entrypoint fails to resolve (a broken loader); `mel_gpu_buffer_device_address`
guards the PFN before calling. The change is uniform across **all** platforms — desktop/MoltenVK
loaders (≥1.1/1.2) resolve the unsuffixed core entrypoints identically, so no desktop regression
in mechanism; only the gratuitous desktop assumption that the loader *exports* them is removed.

## Verification

- `//modules/gpu` **compiles** clean under `--config=android` (clang/NDK r29).
- `//apps/ballgame:melody` and `//apps/hello-gpu:melody` **apks build + sign**.
- The bundled `lib/arm64-v8a/libmelody.so` has **zero** undefined references to the four
  entrypoints (and to the already-dispatched `vkCmdBeginRendering`/`vkCmdPipelineBarrier2`),
  while 1.0 core (`vkCreateInstance`, `vkCreateDevice`, …) remain `U` runtime imports — proven
  with `llvm-nm -D --undefined-only`.
- **Runtime on an API-36 emulator** (SwiftShader Vulkan): both apps log `vulkan instance
  created`, `device created on 'Goldfish GFXStream (SwiftShader…)'`, `barrier lowering:
  synchronization2`, ballgame `swapchain ready: 4 images` — no NULL-deref, no crash. Reaching
  "device created" exercises all three instance PFNs (caps probe + device-create `Features2`).

## Kludges & debt (MEL-ENGINE-VIII — confessing all)

- **`vkGetBufferDeviceAddress` not runtime-exercised.** ballgame/hello-gpu don't request BDA, so
  the device-PFN path is validated only by compile + link + the NULL guard, not a live call. The
  three instance PFNs *are* exercised live. Low risk (identical mechanism to the proven path).
- **Dynamic rendering floored on the emulator**, so the `cmd_begin_rendering` PFN path wasn't
  taken at runtime — but that's the **pre-existing** gfxstream quirk detection
  (`device.c:250`), working as designed, not new debt. On real hardware DR engages.
- **macOS-Vulkan still can't compile on this host** — pre-existing kludge (raw
  `-I/opt/homebrew/include` rejected by Bazel's sandbox as outside the execroot;
  `modules/gpu/BUILD.bazel`). Every vulkan TU fails sandbox path-validation there, so this
  change could not be cross-checked via macos+vulkan; the android clang compile is the
  equivalent proof (same C, same `vulkan.h` core typedefs). Orthogonal; flagged earlier as the
  MoltenVK-host-path pin.
- **`MESA: Failed to open rendernode`** spam from the app process is the emulator's unrelated
  GLES/Mesa path failing headless — not the Vulkan path, which went gfxstream→SwiftShader fine.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document the android GPU run loop for future sessions: `ANDROID_NDK_HOME=<sdk>/ndk/29.0.14206865`
  + `ANDROID_HOME` are required for `--config=android`; a headless emulator boots with
  `emulator -avd <avd> -gpu swiftshader_indirect -no-window -no-snapshot`, and `mel_log` lands
  under logcat tag `gpu`.

## Suggestions

- **win32 link (Track F)** and **wasm GPU** remain the two open frontiers. win32 is gated on
  gmp+mpfr under MSVC ABI (math needs real mpfr — `mpfr_custom_*` — so mini-gmp is out; the slang
  vendored-prebuilt pattern is the likely path), then the `-l`→`.lib` respell + `<dinput.h>`
  gaps. wasm GPU needs a `display` wasm axis + SjLj↔wasm-EH + `-sASYNCIFY=2`.
- The android apks now **build + run** but aren't yet wired into a `bazel test`/CI smoke. A
  headless-emulator launch-and-grep-logcat check (assert `device created`, no `FATAL`) would
  guard against regressions like the proc-addr surface silently regrowing.
