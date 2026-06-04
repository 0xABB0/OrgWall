# Slang per-platform libslang vendoring (task #33, I8)

Goal: make libslang available at runtime on the platforms that lack a desktop prebuilt — android-arm64, ios-arm64, wasm — plus the win32 DXIL signer payload. File ownership: `third-party/slang/**`.

## Per-platform status

- **wasm** — DONE and verified. `slang-wasm` target fetches `slang-2026.10.2-wasm-libs.zip` (the native static-archive artifact, not the embind JS build) via `mel_prebuilt`, and links `slang-compiler core compiler-core cmark-gfm miniz lz4` in `-Wl,--start-group/--end-group`. The artifact is built with native WebAssembly exception handling, so the wrapper TU and the link both carry `-fwasm-exceptions` (else `__cpp_exception` / `_Unwind_CallPersonality` / `__wasm_lpad_context` are undefined). Single-threaded build. Proof: a standalone single-threaded emcc program calling `mel_slang_compile` links the archives clean and **runs under node**, emitting a 204-byte WGSL blob (exit 0). Linked module: 23.5 MB uncompressed / 8.8 MB gz (debug-O0).

- **android-arm64** — recipe VERIFIED, vendoring BLOCKED. The two-phase cross-build is proven to work in this environment and produces a valid arm64 `libslang-compiler.so` (30.7 MB unstripped / 26.0 MB stripped, LLVM disabled by the android preset; deps glslang 8.2 MB, glsl-module 1.5 MB, rt 1.5 MB). But the build does not fit the build graph (see below). `slang` is `mel_unavailable` on android: loud-fail rather than a broken link.

- **ios-arm64** — BLOCKED, same as android, plus there is no upstream cmake preset (a bespoke `arm64-apple-ios` toolchain file is needed). Not attempted in-graph; `mel_unavailable`.

- **win32 DXIL signer** — DXIL emit+sign VERIFIED working; vendored redistributable NOT wired. The slang-compile test passes 10/10 on win-pilot **including the DXIL-emit case** (`MEL_SLANG_EMIT_DXIL` branch requires `b.data != NULL`), so DXIL emits and is signed at runtime — `dxil.dll` + `dxcompiler.dll` resolve from the Windows 10 SDK (`Windows Kits\10\bin\...\x64\`) that `dev.cmd`/vcvars puts on PATH. The Microsoft.Direct3D.DXC redistributable payload (pin 1.9.2602.24) is only strictly needed on end-user machines without the SDK, or for SM 6.5+ newer than the SDK's DXC. It is not in the build graph (see below); the version pin lives in `readme.md`.

- **macos / linux / win32 (desktop)** — UNCHANGED and green. `slang-compile` 10/10 on macos and on win-pilot; `hello-gpu macos --gpu=metal` links + packages.

## Android/iOS source-build recipe (verified for android-arm64)

    git clone --recurse-submodules --branch v2026.10.2 https://github.com/shader-slang/slang
    cd slang
    cmake --workflow --preset generators --fresh
    rm -rf generators && mkdir generators
    cmake --install build --prefix generators --component generators --config Release
    export ANDROID_NDK_HOME=<ndk>          # r28 used
    cmake --preset android-arm64 --fresh \
        -DSLANG_GENERATORS_PATH=$PWD/generators/bin \
        -DPython3_EXECUTABLE=/usr/bin/python3
    cmake --build --preset android-arm64-release

Output: `build-android-arm64-v8a/Release/lib/libslang-compiler.so` (+ `libslang.so` symlink, `libslang-glslang*`, `libslang-glsl-module*`, `libslang-rt`).

Two host-environment gotchas hit and resolved:
- The host generators install must use `--config Release` (the `--workflow --preset generators` build is Release; a bare `--install` looks for Debug and fails).
- The spirv-tools registry-table codegen needs Python with a working `expat`; Homebrew's Python 3.14 ships a broken `pyexpat` (`_XML_SetAllocTrackerActivationThreshold` symbol mismatch vs the system `libexpat`). `-DPython3_EXECUTABLE=/usr/bin/python3` (system 3.9) fixes it.

## Why android/ios/win32-DXC are not in the build graph (needs Gabbo)

The task said "build from source in the build graph (Gabbo's decision)". On contact with the build framework, that collides with three hard constraints; each needs Gabbo:

1. **Two-phase cross-build vs. single-phase `mel_cmake`.** The android/ios libslang build is host-generators-first, then cross with `-DSLANG_GENERATORS_PATH`. `modules/build/thirdparty.c::build_cmake` does a single configure→build→install of one source tree per variant; it has no host-then-cross orchestration, passes no NDK toolchain file, and the `mel_cmake` args are static literals that cannot interpolate the runtime-discovered NDK path. Adding a two-phase host-generator thirdparty pass is a NEW build-framework pass — halt-and-query (CLAUDE.md "Registering a new pass is undocumented"). The design spec's own recommendation (§1, OQ1) is **cross-build-once-and-mirror**: cross-build offline (recipe above), host the `.so` set, fetch via `mel_prebuilt` like the desktop zips. That is the low-risk path and needs only Gabbo's go-ahead to host a mirror per Slang pin.

2. **win32 prebuilt extraction uses `unzip`, absent on the win32 host.** `build_prebuilt` extracts with `unzip`, which is not on win-pilot's PATH even under `dev.cmd` (only `tar`/bsdtar, which extracts the DXC `.nupkg` correctly). Wiring the DXC payload needs the framework extractor to fall back to `tar -xf` on win32 — a `modules/build/thirdparty.c` change (outside `third-party/slang/**`). The current desktop slang DLLs on win-pilot are a cached prefix from an earlier working environment, not a fresh `unzip`.

3. **One prefix per build.c directory; no co-location hook.** `mel_target_outdir` keys the prefix on the build.c directory, not the target name, so every third-party target in `third-party/slang/build.c` shares one prefix and clobbers. There is also no post-fetch hook to co-locate the DXC DLLs into the slang runtime's `bin/` where libslang `LoadLibrary`s them, and win32 has no packaging copy step. A clean DXC vendoring needs either a separate third-party directory plus a co-location mechanism, or a framework enhancement.

## Kludges / debt (MEL-ENGINE-VIII — confess all)

- **Local, uncommitted verification patch.** To prove the slang archives link into the *real* app on wasm (the test runner is an awkward vehicle — it uses `setjmp`, which `-fwasm-exceptions` forces into a mode needing compile-time `-sSUPPORT_LONGJMP`), I temporarily edited `modules/app/src/web/lifecycle_web.c` to fix a **pre-existing** emscripten-5.0.7 signature break (`em_beforeunload_callback` now returns `const char*`). It was reverted (`git checkout`) and never committed — it is not my file. The full hello-gpu wasm link then reached the slang archives and resolved every slang symbol, failing only on an unrelated `--shared-memory` collision (below).
- **wasm slang vs. hello-gpu shared memory — real architectural collision, unresolved.** The `slang-wasm-libs` artifact is single-threaded (no atomics/bulk-memory, per design §1). hello-gpu's wasm target links with `--shared-memory` (`-pthread -sWASM_WORKERS=1`). You cannot link a non-shared-memory archive into a shared-memory module (`--shared-memory disallowed by compile.o`). Upstream ships only the single-threaded wasm-libs. Resolving this is the design's §3.3 intent — run the slang compile in a separate single-threaded Web Worker, NOT linked into the main multithreaded module — which is gpu-module wiring outside `third-party/slang/**`. Needs Gabbo / the gpu owner. So hello-gpu wasm does not yet link slang end-to-end; the slang-only probe proves libslang itself runs in wasm.
- **android artifact is ~26 MB stripped**, marginally over the design's ≤25 MB/arch ceiling; wasm module is ~8.8 MB gz, marginally over the ≤8 MB gz budget. Both are debug-O0 and would shrink under `--release` + LTO. No size-budget build *warning* was added: that needs a build-framework hook (no such mechanism exists for third-party link-size), so it is recorded here, not faked in code.
- **No comments in build.c** (per CLAUDE.md). The rationale that would have been comments lives in `third-party/slang/readme.md`.

## CLAUDE.md suggestions (recommendations only)

- Document, in `modules/build/platforms.md`, that all third-party targets sharing one `build.c` directory share one prefix (`mel_target_outdir` keys on directory), so multiple `mel_prebuilt` payloads need separate directories.
- Note that `mel_prebuilt` extraction depends on `unzip`, which is absent on the win32 build host; consider `tar -xf` as the portable extractor.

## Suggestions

- Land the framework changes for the three blockers above, in priority order: (a) cross-build-and-mirror for android/ios libslang (lowest risk, design-sanctioned); (b) `unzip`→`tar` fallback so win32 `mel_prebuilt` works at all; (c) a per-target prefix or post-fetch co-location hook for the DXC redistributable.
- The wasm slang-in-a-worker bridge (design §3.3) is the real unblocker for hello-gpu wasm; sequence it in the gpu module.
- Fix the pre-existing `modules/app/src/web/lifecycle_web.c` emscripten-5.0.7 `em_beforeunload_callback` signature break — it currently breaks every wasm app link.
