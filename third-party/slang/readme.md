# slang

C wrapper (`src/compile.cpp`, `include/slang/compile.h`) over libslang's C++ API. The engine consumes only the C surface: `mel_slang_compile` / `mel_slang_compile_reflect` emit SPIRV / MSL / WGSL / DXIL from a `.slang` source string at runtime.

`SLANG_VERSION` in `build.c` pins the upstream release; the wrapper links the fully-versioned library (ABI is unstable, major pinned 0). DXC payload version pins in `DXC_VERSION`.

## Per-platform libslang vendoring

Upstream GitHub releases ship prebuilt libslang for `{windows, linux, macos} x {x86_64, aarch64}` only. The release zip carries a `libslang.{dylib,so}` / `slang.lib` symlink onto the versioned `libslang-compiler`, so `-lslang` resolves there.

- **macos / linux / win32**: `slang-runtime` target; `mel_prebuilt` arms per target platform x arch (the variant picks the arm, so cross-building linux from macos fetches the linux zip). Links `-lslang`. win32 extracts with `tar` (bsdtar); `unzip` is absent on the win32 build host.
- **android**: `slang-android` target; locally vendored `tools/build/vendor/slang/slang-<ver>-android-aarch64.zip` fetched via `file://` from the repo root. Produced by `tools/build/vendor/slang/build-android-arm64.sh` (NDK cross-build, generators host pass, shared `libslang-compiler.so` with STL linked static — self-contained, only NEEDs libc/libm/libdl). The APK packaging step ships every dependent third-party `prefix/lib/*.so` into `jniLibs`. Links `-lslang-compiler`; the wrapper TU links `-lc++_static -lc++abi`.
- **ios (simulator, arm64)**: `slang-ios` target; vendored `slang-<ver>-ios-sim-aarch64.zip` from `build-ios-sim-arm64.sh` (bespoke `CMAKE_SYSTEM_NAME=iOS` configure, no upstream preset; `CMAKE_MACOSX_BUNDLE=OFF`, split-debug-info off). Ships a single `@rpath/libslang-compiler.dylib`; apple packaging embeds dependent third-party dylibs into the `.app/Frameworks` and the target rpaths `@executable_path/Frameworks`. A device (non-simulator) artifact does not exist yet; device links loud-fail on the simulator slice.
- **wasm**: `slang-wasm` target; vendored `slang-<ver>-wasm-mt.zip` from `build-wasm-mt.sh`. The upstream `slang-wasm-libs` artifact is single-threaded and cannot link into melody's `-pthread` / shared-memory wasm world (wasm-ld refuses to mix atomics-less objects), so we build the static archives ourselves with `-pthread -fwasm-exceptions -Os`, debug-stripped. Archives link in dependency order inside `--start-group/--end-group`: `slang-compiler compiler-core core cmark-gfm miniz lz4`. The gpu module's wasm link uses `-sASYNCIFY=2` (JSPI): binaryen's ASYNCIFY=1 instrumentation pass OOMs (>30 GB) over a binary containing all of slang.

All vendor scripts pin `SLANG_VERSION` from `SLANG_VERSION.lock` and are reproducible one-command builds; the zips are committed (10-15 MB each). Publishing them to a hosted mirror (and fetching like the desktop zips) remains the eventual shape; the `file://` vendor lane is the local mirror until then.

### win32 DXIL signer (not yet wired)

Slang emits HLSL and hands off to `dxcompiler.dll`, the result signed by `dxil.dll`, which D3D12 requires for shader creation outside Developer Mode; neither ships in the slang release zip nor is recent enough in the OS. The signed-DXIL payload is the `Microsoft.Direct3D.DXC` NuGet `.nupkg` (a plain zip); `build/native/bin/x64/{dxcompiler,dxil}.dll` is what we need. Pin: `1.9.2602.24`, fetched from `https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.dxc/<ver>/microsoft.direct3d.dxc.<ver>.nupkg`.

Remaining gap: every third-party target declared in one `build.c` shares one prefix and there is no post-fetch hook to co-locate the DLLs into the slang runtime's `bin/` where libslang `LoadLibrary`s them; win32 also has no packaging copy step. The DXC DLLs are a **runtime** LoadLibrary dependency, not a link dependency. Until that lands, provision `{dxcompiler,dxil}.dll` next to the win32 executable (or on PATH) out-of-band.

## Tests

`slang-compile` (`test/compile_test.c`) covers the four emit targets (host-emittable ones) plus reflection. DXIL loud-fails off-win32 by design (`MEL_SLANG_EMIT_DXIL` gated to win32).
