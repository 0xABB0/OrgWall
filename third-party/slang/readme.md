# slang

C wrapper (`src/compile.cpp`, `include/slang/compile.h`) over libslang's C++ API. The engine consumes only the C surface: `mel_slang_compile` / `mel_slang_compile_reflect` emit SPIRV / MSL / WGSL / DXIL from a `.slang` source string at runtime.

`SLANG_VERSION` in `build.c` pins the upstream release; the wrapper links the fully-versioned library (ABI is unstable, major pinned 0). DXC payload version pins in `DXC_VERSION`.

## Per-platform libslang vendoring

Upstream GitHub releases ship prebuilt libslang for `{windows, linux, macos} x {x86_64, aarch64}` only. The release zip carries a `libslang.{dylib,so}` / `slang.lib` symlink onto the versioned `libslang-compiler`, so `-lslang` resolves on those hosts.

- **macos / linux / win32**: `slang-runtime` target, `mel_prebuilt` per host (C-preprocessor host-gated, since the build runs on the host). Links `-lslang`.
- **wasm**: `slang-wasm` target, `mel_prebuilt` of `slang-<ver>-wasm-libs.zip` — the native static-archive artifact (headers + `.a` for direct C++ linkage under emcc), not the embind JS build. Archives link in dependency order inside `--Wl,--start-group/--end-group`: `slang-compiler compiler-core core cmark-gfm miniz lz4`. The artifact is built with native WebAssembly exception handling, so the wrapper TU and the link both carry `-fwasm-exceptions` (else `__cpp_exception` / `_Unwind_CallPersonality` are undefined). It is a **single-threaded** build (no atomics / shared memory).
### win32 DXIL signer (not yet wired)

Slang emits HLSL and hands off to `dxcompiler.dll`, the result signed by `dxil.dll`, which D3D12 requires for shader creation outside Developer Mode; neither ships in the slang release zip nor is recent enough in the OS. The signed-DXIL payload is the `Microsoft.Direct3D.DXC` NuGet `.nupkg` (a plain zip); `build/native/bin/x64/{dxcompiler,dxil}.dll` is what we need. Pin: `1.9.2602.24`, fetched from `https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.dxc/<ver>/microsoft.direct3d.dxc.<ver>.nupkg`.

It is **not** in the build graph because two mechanisms are missing:

1. `mel_prebuilt` extracts with `unzip`, which is absent on the win32 build host (only `tar` / bsdtar is present, and it extracts the `.nupkg` correctly). The framework extractor must fall back to `tar -xf` on win32 (a `modules/build/thirdparty.c` change).
2. Every third-party target declared in one `build.c` shares one prefix (`mel_target_outdir` keys on the build.c directory, not the target name), and there is no post-fetch hook to co-locate the DLLs into the slang runtime's `bin/` where libslang `LoadLibrary`s them. win32 also has no packaging copy step.

The DXC DLLs are a **runtime** LoadLibrary dependency, not a link dependency — the slang lib links on win32 without them; they are only needed when a DXIL emit actually runs. Until the two mechanisms land, provision `{dxcompiler,dxil}.dll` next to the win32 executable (or on PATH) out-of-band.

## android-arm64 / ios-arm64

No upstream prebuilt. `slang` is marked `mel_unavailable` on these platforms: the source-build does not fit the build graph and a broken or silently-missing link is forbidden (MEL-ENGINE-VIII).

The libslang source-build is a **two-phase cross-compile** that the single-phase `mel_cmake` thirdparty mechanism cannot orchestrate: host generators must be built first, then the cross target links against `-DSLANG_GENERATORS_PATH`. iOS additionally has no upstream cmake preset (bespoke toolchain file required).

Verified android-arm64 recipe (NDK r28; produces `libslang-compiler.so`, arm64, ~32 MB unstripped / ~27 MB stripped, LLVM disabled by the preset):

    git clone --recurse-submodules --branch v<ver> https://github.com/shader-slang/slang
    cd slang
    cmake --workflow --preset generators --fresh
    rm -rf generators && mkdir generators
    cmake --install build --prefix generators --component generators --config Release
    export ANDROID_NDK_HOME=<ndk>
    cmake --preset android-arm64 --fresh \
        -DSLANG_GENERATORS_PATH=$PWD/generators/bin \
        -DPython3_EXECUTABLE=/usr/bin/python3
    cmake --build --preset android-arm64-release

(`-DPython3_EXECUTABLE` pins a Python with a working `expat`; the spirv-tools registry-table codegen needs XML parsing.)

To make these platforms link, the cross-built artifact must be mirrored and fetched via `mel_prebuilt` like the desktop zips, or a two-phase host-generator-then-cross thirdparty pass must be added to the build framework. Both are Gabbo decisions.

## Tests

`slang-compile` (`test/compile_test.c`) covers the four emit targets (host-emittable ones) plus reflection. DXIL loud-fails off-win32 by design (`MEL_SLANG_EMIT_DXIL` gated to win32).
