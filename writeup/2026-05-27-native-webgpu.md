# Native WebGPU (Dawn) on desktop — macOS delivered, Android wired

Until now the `webgpu` backend was reachable only under Emscripten: `device.c`
called `emscripten_webgpu_get_device()` and `swapchain.c` built its surface from
an HTML canvas selector. This session lifted the backend onto native targets via
Dawn, so `--gpu webgpu` is a real choice on macOS (verified) and Android (wired,
unverified). The web path is untouched.

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
- The actual `-lwebgpu_dawn` is gpu-gated to `webgpu`, so metal/vulkan builds
  inherit only the (empty, harmless) `-L`/`-I` that third-party deps propagate
  unconditionally, never the library.

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
- `command.c`/`buffer.c`/`pipeline.c`/`shader.c`: unchanged. `command.c` already
  carried the native `wgpuSurfacePresent` branch, so steady-state rendering
  needed no work.

### Verification
`./nob build hello-gpu macos --gpu webgpu` links cleanly. The running app shows
the triangle (confirmed by Gabbo). Two proofs it is WebGPU and not a Metal
fallback: `ar t libmelody.a` carries only `gpu/src/webgpu/*` objects (no
metal/vulkan compiled in), and `vmmap` of the live process shows
`libwebgpu_dawn.dylib` mapped. Device creation idles at ~4% CPU in a sleeping
state, so the adapter-acquire loop terminates rather than busy-spinning.

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
- **Android is wired, not exercised.** The from-source path has not been run
  once. The cross→Dawn-CMake hand-off, the monolithic-shared install layout
  landing `libwebgpu_dawn.so` where the prefix expects it, and the
  `DAWN_FETCH_DEPENDENCIES` clone all remain unverified. The chromium/7187 pin
  was chosen for desktop-prebuilt parity, not because that tag is known to build
  cleanly from source under the NDK.
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
  is now stale for macOS. Worth a line in `platforms.md` recording that webgpu
  is a selectable native backend on macOS (prebuilt Dawn) and Android
  (from-source), and updating the `valid_gpu_backends` prose.
- No CLAUDE.md change proposed beyond doc-content refresh; the root and module
  CLAUDE.md instructions held up fine for this task.

## Suggestions

- **Verify the Android build before claiming the axis.** Run
  `./nob build hello-gpu android --gpu webgpu` to shake out the cross→Dawn-CMake
  seam. Expect a long first build and likely a few `-DTINT_*`/`-DDAWN_*` toggles
  to prune the dependency download. Until then, treat Android webgpu as
  scaffolding.
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
