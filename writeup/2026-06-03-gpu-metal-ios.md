# GPU RHI — Metal backend on iOS (surface + build + simulator run)

## Work done

Brought the working macOS Metal backend to the iOS simulator. The render core is reused
verbatim; the deltas are the surface (UIView-hosted `CAMetalLayer`), platform gating of the
three macOS-only Metal/AppKit APIs, the build target, and proving render-to-texture readback on
the iOS simulator.

### iOS surface (`src/metal/ios/surface.m`, new)
UIKit analogue of the cocoa surface: takes a `UIView` native handle, hosts a `CAMetalLayer`
(reusing the view's layer if it already is one, else adding a sublayer), wires `device`,
`pixelFormat=BGRA8Unorm`, `framebufferOnly`, `contentsScale`, `drawableSize`. Scale comes from
`view.window.screen.scale` (fallback `UIScreen.mainScreen.scale`). `mel_gpu_surface_create` /
`_destroy` / `_reconfigure` mirror the macOS contract. No `wantsLayer` (UIView is always
layer-backed). Loud-rejects null device/native and a non-UIView handle.

### Platform gating of macOS-only Metal APIs (flagged macOS-file edits)
Three Metal/AppKit symbols exist only on macOS; each is gated behind `TARGET_OS_OSX`
(from `<TargetConditionals.h>`, pulled transitively by `<Metal/Metal.h>`):
- `instance.m`: `MTLCopyAllDevices()` is macOS-only. On iOS the device list comes solely from
  `MTLCreateSystemDefaultDevice()` (the existing `count==0` fallback path). The simulator yields
  one adapter, `name="Apple iOS simulator GPU"`.
- `caps.m`: `id<MTLDevice>.lowPower` is `API_UNAVAILABLE(ios)`. On iOS the adapter is unconditionally
  `MEL_GPU_ADAPTER_INTEGRATED` (mobile GPUs are integrated/unified by construction).
- `caps.m` + `device.m`: `recommendedMaxWorkingSetSize` is `ios(16.0)`-introduced; wrapped in
  `@available(macOS 10.12, iOS 16.0, *)` so the working-set budget degrades to 0 below that
  deployment floor rather than warning/erroring.
- `swapchain.m`: `CAMetalLayer.displaySyncEnabled` is macOS-only (iOS always presents on vsync,
  with no off switch). Gated; on iOS a `vsync=off` request emits a loud warn that the request is
  not honourable rather than silently ignoring it (MEL-ENGINE-VIII / MEL-CODE-007).

### ARC over-release bug fixed (shared backend; surfaced by iOS, latent on macOS)
`mel_alloc_type` does **not** zero. Four backend structs carry `__strong` ObjC fields and were
allocated uninitialised then assigned `*x = (T){ 0 }` (or a designated initializer). Under ARC,
that aggregate assignment runs the compiler's struct-copy helper, which **releases the prior
(garbage) ObjC pointers** before storing the new ones. On the iOS simulator that garbage is a
non-nil, PA-signed pointer → `objc_release` SIGBUS/`EXC_BAD_ACCESS` inside `device_create` /
`command_list_create`. macOS happened to survive (released-as-nil garbage), so it was latent.
Fix: allocate with `mel_calloc` (zeroed) and set fields individually, so every `__strong id`
starts nil and ARC releases are no-ops.
- `device.m` (`Mel_Gpu_Device`: `mtl`, `queue`)
- `record.m` (`Mel_Gpu_Command_List`: `cb`, `encoder`, `index_buffer`)
- `swapchain.m` (`Mel_Gpu_Swapchain`: `drawable`)
- `ios/surface.m` (`Mel_Gpu_Surface`: `layer`)

Resource objects (`buffer`/`texture`/`shader`/`pipeline`/`sampler`) store ObjC as `void*` via
`__bridge_retained`, so they are ARC-invisible and were never affected.

### build.c (additive)
- iOS+metal lib gating: glob `src/metal/macos/*.m` for IOS, `mel_exclude_source` the AppKit-only
  `macos/surface.m`, add `src/metal/ios/*.m`; link frameworks `Metal, QuartzCore, Foundation, UIKit`
  (UIKit replaces macOS's AppKit).
- `gpu-metal` test: iOS-only extra source `test/ios_stacktrace_shim.c` (see Kludges).

### Run on the iOS simulator
`nob test` runs the test binary on the host (`spawn(bin)`), which cannot execute an
iphonesimulator-SDK Mach-O. So the proof path is: `nob compile gpu-metal ios --gpu=metal` to
produce the sim binary, boot a sim (`xcrun simctl bootstatus`), then
`SIMCTL_CHILD_MEL_TEST_NOFORK=1 xcrun simctl spawn <udid> <bin>`. `MEL_TEST_NOFORK=1` is required
for the same reason as macOS/MoltenVK: the runner forks per test and Metal/ObjC state initialised
in the parent triggers `objc_initializeAfterForkError` in the child.

Result on `iPhone 16` sim (iOS 26, `Apple iOS simulator GPU`): **4 passed, 0 failed**, exit 0 —
MSL-passthrough caps assertion + triangle/gradient/quad each rendered from its MSL bundle and read
back with the identical analytic pixel checks as macOS. The PPM dumps warn-skip (the sim sandbox
can't write the hard-coded host `build/macos-debug` path); the assertions are independent of the
dump.

## Verification
- macOS metal (regression guard): `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test
  gpu-metal macos --gpu=metal` → **4/4**, twice (before and after the calloc fixes).
- macOS metal hello-gpu: `./nob build hello-gpu macos --gpu=metal` → LINK OK, `.app` packaged.
- macOS vulkan gpu-foundation → 13/13 (build.c change does not disturb the vulkan path).
- iOS sim gpu-metal: compile + `simctl spawn` → **4/4**, exit 0.
- All edited/new files clang-format clean (Apple clang-format 17).

## iOS Metal caps (honest, no silent defaults)
Probed live on the simulator (`Apple iOS simulator GPU`, iOS 26):
- adapter_type = INTEGRATED; `hasUnifiedMemory` = **false** on the sim → host-visible-device-local =
  NONE (real A14+ silicon is full UMA; the simulator is a lower tier).
- `recommendedMaxWorkingSetSize` = **0** on the sim → device_local_bytes = host_visible_bytes = 0
  (no working-set budget granted; honest 0, not a fabricated number).
- shader: msl=1, spirv=0, fp16=1, int64=0, wave_ops=1, subgroup 32..32.
- sampler anisotropy=1 (16x); raster tile_local=NATIVE; queues timeline=NATIVE, async_compute=1;
  bindless tier=NONE.

Caveat: the shader/raster/queue caps above are **statically declared** in `caps.m` (the Metal
feature ceiling), not per-device-probed. On the simulator's software GPU they assert a tier the sim
may not truly back; the two values that *are* device-queried (`hasUnifiedMemory`, working-set)
already reveal the simulator's reduced grant. The spec's Metal-4 ceiling (A14+/iOS 26 silicon) is a
different tier from the simulator; nothing here claims the sim is that tier.

## Cross-module blockers (iOS analogue of the linux blocker — outside this task's file ownership)
`./nob build hello-gpu ios --gpu=metal` links the **entire** Metal backend (zero missing `mel_gpu_*`
symbols) and fails only on two non-GPU symbols:
1. `mel_gpu_view_create_opt` / `mel_gpu_view_surface` — the gui module's **uikit backend has no
   `gpu_view.m`** (cocoa/dom/winui/androidnative each have one; uikit does not). This blocks
   hello-gpu's windowed iOS path. A `modules/gui/src/uikit/gpu_view.m` (a `UIView` subclass +
   `mel_gpu_view_surface` returning the native handle, feeding the iOS surface above) is the fix —
   a gui-module task.
2. `mel__platform_stacktrace_capture` — the **debug module has no iOS stacktrace backend**.
   `debug/build.c` gates `src/macos/*.c` to MACOS only, and `src/macos/stacktrace.c` has a hard
   `#if !MEL_PLATFORM_OSX #error`. The macOS impl is pure Darwin/POSIX (`backtrace`+`dladdr`) and
   would work on iOS verbatim; it only needs the build gating widened to Apple/POSIX and the
   `#error` relaxed. A debug-module task.

Both are the iOS equivalent of the linux Vulkan-headers blocker: the GPU/Metal layer is complete,
a sibling module lacks its iOS lane.

## Kludges / debt (confessed — bar zero, MEL-ENGINE-VIII)
1. **`test/ios_stacktrace_shim.c` (test scaffolding).** The `gpu-metal` test links `debug`, whose
   `assert` path calls `mel_stacktrace_capture` → `mel__platform_stacktrace_capture`, which has no
   iOS impl (blocker #2). To run the readback test on the sim **without editing the debug module**
   (outside my ownership), I supply a tiny iOS-only TU defining that symbol via the same real
   `backtrace()`/`dladdr()` the macOS impl uses. It is a real capture, not a fake — but it is a
   duplicate that will drift. **Delete it** the moment the debug module grows its iOS lane
   (blocker #2); then the test links the canonical impl.
2. **iOS swapchain present path unproven.** Only the headless render-to-texture readback is proven
   on the sim. The `CAMetalLayer`→`nextDrawable`→present windowed path needs the gui-uikit
   `gpu_view` (blocker #1) to surface a `UIView`; it could not be exercised this session.
3. **PPM dump path is hard-coded to `build/macos-debug`.** Pre-existing in `test_metal.c`; on iOS
   it warn-skips (sandbox). Not load-bearing, but the path is wrong for any non-macOS run.
4. **Static caps on the simulator.** As noted, shader/raster/queue caps are declared, not probed;
   on the sim they over-claim the tier. A real per-device probe (feature-set families) is the
   correct long-term fix and is backend-wide, not iOS-specific.

## Shared macOS-file edits (flagged, as required)
All inside `modules/gpu/src/metal/macos/` (the convention is that this dir is the shared Metal
backend; iOS reuses it):
- `instance.m` — `MTLCopyAllDevices()` gated `TARGET_OS_OSX`.
- `caps.m` — `lowPower` gated; `recommendedMaxWorkingSetSize` `@available`-guarded.
- `device.m` — `recommendedMaxWorkingSetSize` `@available`-guarded; **calloc fix** (ARC bug).
- `swapchain.m` — `displaySyncEnabled` gated; **calloc fix**.
- `record.m` — **calloc fix** (command list).
The calloc fixes are correctness fixes for the shared backend (latent macOS bug), not iOS-only
shims; macOS stays 4/4.

## CLAUDE.md suggestions (recommendations only — not applied)
- Document running iOS-sim tests: `nob test` cannot spawn an iphonesimulator binary on the host;
  the pattern is `nob compile <test> ios` + `SIMCTL_CHILD_MEL_TEST_NOFORK=1 xcrun simctl spawn`.
  Worth a "Test invocations" note alongside the macOS `MEL_TEST_NOFORK=1` recommendation.

## Suggestions
- Teach the `nob test` driver an iOS-simulator lane: when the variant is `ios && simulator`, run the
  test binary via `xcrun simctl spawn` (with `SIMCTL_CHILD_MEL_TEST_NOFORK=1`) instead of bare
  `spawn(bin)`. That makes `./nob test gpu-metal ios --gpu=metal` work end-to-end.
- Land the debug-module iOS stacktrace lane (gate `src/macos/stacktrace.c` for Apple/POSIX, relax
  the `#error`); then delete `ios_stacktrace_shim.c`.
- Add `modules/gui/src/uikit/gpu_view.m` to unblock the windowed hello-gpu iOS path.
- Make `mel_alloc_type` zero, or audit every ObjC-bearing struct allocated with it — the ARC
  over-release class of bug is silent on macOS and only bites on iOS/PA-hardened targets.
