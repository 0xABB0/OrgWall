# Native WebGPU (Dawn) — macOS and Android, both verified on hardware

Until now the `webgpu` backend was reachable only under Emscripten: `device.c`
called `emscripten_webgpu_get_device()` and `swapchain.c` built its surface from
an HTML canvas selector. This session lifted the backend onto native targets via
Dawn, so `--gpu webgpu` is a real choice on macOS (prebuilt Dawn) and Android
(Dawn built from source). The web path is untouched. Both render the
`hello-triangle` on hardware: macOS, and a Pixel 4a (arm64-v8a, Vulkan-backed
Dawn on the Adreno driver).

## Work done

### Build axis — webgpu as an opt-in native backend
- `tools/build/runner_platform.c`: added `webgpu` to `k_macos_gpu` and
  `k_android_gpu`, placed after the existing defaults so the host default stays
  metal/vulkan and webgpu is selected only by `--gpu webgpu`.
- `tools/build/build.{c,h}`: exposed `mel_build_ctx_gpu_backend(ctx)` (the
  internal `ctx->gpu_backend` was already populated) so a third-party
  `on_compile` can decide whether to provision Dawn.

### Dawn provisioning — `third-party/webgpu/build.c`
A `MEL_TARGET_THIRD_PARTY` target restricted to `{macos, android}`. Provisioning
diverges by platform because no prebuilt Dawn targets Android — the eliemichel
mirror, jspanchu, and ifl-studio all publish desktop slices only, and the Dawn
team withholds prebuilts pending webgpu.h ABI stabilization.
- **macOS**: fetches eliemichel's prebuilt `Dawn-7187-macos-<arch>-Release.zip`
  and expands it into the target prefix. Pinned to chromium/7187 to match the
  prior `flappy_ball_genetic` integration.
- **Android**: shallow-clones Dawn at `chromium/7187` and drives `mel_tp_cmake`
  with `-DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED -DDAWN_FETCH_DEPENDENCIES=ON`,
  reusing the NDK toolchain `mel_tp_cmake` already injects from `ctx->cross`.
  Two further configure flags were needed to make Dawn build cleanly here:
  `-DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF` (Android wants only
  the Vulkan backend, and the GL path drags in an OpenGL loader generator) and
  `-DPython3_EXECUTABLE=/usr/bin/python3` (Dawn's generators parse XML via
  Python's `pyexpat`; the Homebrew `python3` on this host ships without it,
  while the macOS system interpreter has it).
- The actual `-lwebgpu_dawn` is gpu-gated to `webgpu`, so metal/vulkan builds
  inherit only the (empty, harmless) `-L`/`-I` that third-party deps propagate
  unconditionally, never the library.

### Android pipeline fixes — `tools/build/runner_android.c`
Two defects in the per-ABI Android build path surfaced only when actually
building/running for the device:
- The per-ABI third-party build context was zero-initialized and never carried
  `gpu_backend`, so the Dawn `on_compile` saw `NULL` and silently no-op'd,
  leaving an empty prefix and a missing-header failure when melody's webgpu
  sources compiled. Fixed by propagating `g_gpu_backend` into that context, the
  Android-side analogue of what `context_init` already did elsewhere.
- The APK bundled only `libmelody.so`. Android has no rpath/`@loader_path`, so
  the runtime loader could not resolve `libwebgpu_dawn.so` and crashed at launch
  with `UnsatisfiedLinkError`. Fixed by copying every third-party `.so` (just
  Dawn's today) into `jniLibs/<abi>/` beside `libmelody.so`, after the per-ABI
  third-party build completes.

Two reusable framework helpers were added rather than open-coding shell calls in
the module (which sees only the `mel_*` API, not nob): `mel_tp_fetch_prebuilt`
(curl + unzip into the prefix) and `mel_tp_fetch_git` (idempotent shallow clone).

### Consumer wiring — `modules/build.c`
- `mel_build_add_dependency(t, "webgpu")` unconditionally; `target_supports`
  drops it off the non-{macos,android} platforms and `on_compile` no-ops unless
  gpu==webgpu.
- A macOS `@rpath` entry so the `@rpath/libwebgpu_dawn.dylib` install name
  resolves against the in-tree prefix.
- Per-platform source exclusions for the three new surface translation units.

### Backend code — `modules/gpu/src/webgpu/`
- `device.c`: the Emscripten body is now under `#if defined(__EMSCRIPTEN__)`;
  the native `#else` performs a blocking `requestAdapter`/`requestDevice` using
  `WGPUCallbackMode_AllowProcessEvents` and pumping `wgpuInstanceProcessEvents`,
  satisfying the synchronous device-creation contract the browser meets with a
  preinitialized device.
- Surface creation was hoisted behind `mel_gpu__webgpu_surface_create(instance,
  native_window)`, declared in `webgpu_backend.h`, with one implementation per
  platform: `surface_cocoa.m` (NSView → CAMetalLayer →
  `WGPUSurfaceSourceMetalLayer`), `surface_android.c` (ANativeWindow →
  `WGPUSurfaceSourceAndroidNativeWindow`), `surface_web.c` (the former canvas
  path). `swapchain.c` now calls the helper and guards its Emscripten-only
  canvas-resize and selector copy.
- Swapchain format negotiation: the hardcoded `BGRA8Unorm` default is fine on
  Metal but unrepresentable in Android's gralloc (no BGRA `AHardwareBuffer`
  mapping), so the surface texture would not allocate and the view stayed blank.
  `swapchain.c` now, on native, queries `wgpuSurfaceGetCapabilities` and adopts a
  surface-advertised format, preferring a plain RGBA8/BGRA8 — RGBA8 on Android,
  BGRA8 on macOS. This required retaining the adapter on `Mel_Gpu_Device`
  (released before), and a reverse `WGPUTextureFormat → Mel_Gpu_Format` map so
  the negotiated format reaches pipeline creation through
  `mel_gpu_swapchain_format`.
- `command.c`/`buffer.c`/`pipeline.c`/`shader.c`: unchanged. `command.c` already
  carried the native `wgpuSurfacePresent` branch, so steady-state rendering
  needed no work.

### Verification
Both targets render the `hello-triangle` on hardware.
- **macOS** (`./nob run hello-gpu macos --gpu webgpu`): the triangle draws. Two
  proofs it is WebGPU and not a Metal fallback: `ar t libmelody.a` carries only
  `gpu/src/webgpu/*` objects (no metal/vulkan compiled in), and `vmmap` of the
  live process shows `libwebgpu_dawn.dylib` mapped. Device creation idles at
  ~4% CPU in a sleeping state, so the adapter-acquire loop terminates rather
  than busy-spinning.
- **Android** (`./nob run hello-gpu android --gpu webgpu`, Pixel 4a, arm64-v8a):
  the gradient triangle draws into the `mel_gpu_view` beside the native label.
  `logcat` shows Dawn on the Adreno Vulkan driver (`AdrenoVK-0`,
  `/vendor/lib64/hw/vulkan.adreno.so`). The residual `4×4 format 56` gralloc
  errors are Dawn probing `AHardwareBuffer` format support at init — non-fatal,
  present even on the host screen, independent of rendering.

## Kludges

- **macOS rpath is layout-coupled.** `@loader_path/../../../third-party/macos/webgpu/lib`
  hard-codes the depth from `build/macos/<config>/<target>/` to the third-party
  prefix. It breaks if the output layout changes or the binary is relocated out
  of the tree. Debt: a packaged macOS app needs the dylib copied into the bundle
  and an `@executable_path`-relative (or re-signed) install name; there is no
  link/package hook doing that today.
- **Busy-wait device acquisition.** `while (!done) wgpuInstanceProcessEvents()`
  spins without the reference's `sleep`. Harmless for a one-shot at startup
  (resolves in milliseconds), but it is a hot loop; if device creation ever
  stalls it pegs a core.
- **Hardcoded host Python path for Dawn's generators.**
  `-DPython3_EXECUTABLE=/usr/bin/python3` is baked into the Android cmake args to
  dodge a Homebrew `python3` that lacks `pyexpat`. It assumes a macOS build host
  with a working system interpreter; on a Linux CI host or a machine without
  that path it is wrong. Debt: detect a `pyexpat`-capable interpreter rather than
  pinning one.
- **Android Dawn build is single-ABI in practice but built per-ABI.** The
  emit loop builds every `k_android_abis` entry, so an `x86_64` slice is built
  too (a second ~20-min from-source Dawn). Only `arm64-v8a` was exercised on
  device; the `x86_64` Dawn compiled but was not run.
- **325 MB debug `.so` shipped in the APK.** The from-source monolithic Dawn is
  an unstripped debug build; it bloats the APK enormously. No release/stripped
  variant selection yet.
- **Single-arch macOS slice.** Arch is resolved at module-compile time via
  `__aarch64__`, so a cross or universal build would fetch the wrong slice. No
  fat-binary handling.
- **`mel_tp_fetch_prebuilt` assumes a flat archive.** It expands straight into
  the prefix and trusts the zip to root at `include/` + `lib/`. A nested-top-dir
  archive would silently misplace files; the only guard is the `produced_lib`
  existence check.
- **Version pin is a literal in three places.** `DAWN_VERSION` governs the URL,
  the git tag, and the implied header ABI, but nothing checks the fetched
  headers match what the backend code compiles against.

## CLAUDE.md suggestions (recommendations only)

- `docs/` and `tools/build/platforms.md` both assert WebGPU is "web only, via
  the Emscripten emdawnwebgpu port" and that DX12/native-webgpu are absent. That
  is now stale: webgpu is a verified, selectable native backend on macOS
  (prebuilt Dawn) and Android (Dawn from source). Worth refreshing that section
  and the `valid_gpu_backends` prose, and adding the host prerequisites
  (CMake + a `pyexpat`-capable Python + Git for the Android from-source path).
- No CLAUDE.md change proposed beyond doc-content refresh; the root and module
  CLAUDE.md instructions held up fine for this task.

## Suggestions

- **Build a release/stripped Dawn for Android.** The shipped `.so` is a 325 MB
  unstripped debug build. Drive the from-source build at `CMAKE_BUILD_TYPE=Release`
  (and/or strip in the `jniLibs` copy) before this is shippable.
- **Detect a `pyexpat`-capable Python instead of pinning one.** Replace the
  hardcoded `/usr/bin/python3` with a probe so the Android build is not bound to
  a macOS host layout.
- **Generalize the prebuilt fetch into the cache.** `mel_tp_fetch_prebuilt`
  duplicates download/extract logic that the content-addressed cache
  (`./nob cache`) could own, keyed on the URL, giving deduplication and `gc`.
- **Adopt a packaging step for native dylib deps.** The rpath kludge recurs for
  any future shared-library dependency (Dawn today, others later). A
  link/package hook that stages dylibs beside the artifact and rewrites install
  names would retire it generally.
- **Query and log the adapter.** A debug-only `wgpuAdapterGetInfo` dump
  (backend type, device name) at device creation would make "which backend am I
  really on" answerable without `vmmap`, and serves MEL-ENGINE-VIII.
- **Pin Dawn once.** Hoist `DAWN_VERSION` to a single shared definition and add
  a header-ABI assertion (e.g. a known `WGPU_*` constant) so a version bump that
  desyncs headers from the backend code fails loudly at compile.
- **`.gitignore` the Android clone.** `third-party/webgpu/dawn/` is a fetched
  tree; ensure it is ignored so it never lands in a commit.
