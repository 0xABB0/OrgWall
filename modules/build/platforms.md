# Build system — platforms, axes, authoring

Operational reference for `nob`, the Melody build system. The framework lives in
`modules/build/` (it is *not* a discovered module — `modules/build/` has no `build.c` and is
skipped by discovery; it is the runner itself, unity-included by the root `nob.c`). Design
rationale lives in `writeup/2026-05-31-build-system-rewrite.md`; this file documents what the
committed system actually does.

`nob` is a self-rebuilding C program: editing any `modules/build/*.c` (or `nob.c`) makes the next
`./nob …` recompile itself first. Always invoke from the repo root.

## Model in one paragraph

A `build.c` per target declares **named artifacts** built from sources and exposed to dependents.
`nob` discovers every `build.c`, runs each one's `build()` to construct the artifact graph,
resolves a single **variant** `(platform, arch, config, backend, gpu, runtime)`, propagates flags
along the explicit dependency graph, and emits a `ninja` file it then runs. There is no implicit
source discovery and no directory-shadowing magic — a target sees exactly the sources, includes,
flags and dependencies its `build.c` declares, each gated by an explicit `Mel_When` selector.

## Authoring a `build.c`

Every `modules/<m>/`, `apps/<app>/` and `third-party/<lib>/` carries a `build.c` exporting one
function:

    #include "build.h"

    void build(Mel_Build *b) {
        Mel_Target *lib = mel_add_library(b, "rng");
        mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
        mel_sources(lib, ALWAYS, "src/*.c");
        mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(LINUX)), "src/posix/*.c");
        mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
        mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lbcrypt");
        mel_depends(lib, "core");
    }

A single `build.c` may declare several artifacts (a library, its host-tool, an example, tests).

**Artifact kinds.** `mel_add_library`, `mel_add_executable`, `mel_add_third_party`,
`mel_add_host_tool` (built for the build machine with plain `clang`, used by codegen),
`mel_add_test` (an executable flagged as a test; `nob test` runs it).

**The selector.** Every property is gated by a `Mel_When`. `ALWAYS` matches every variant;
`WHEN(.field = …, …)` matches when each named field equals the resolved variant. Fields:
`.platforms` (a bitmask — OR `MEL_ON(MACOS|IOS|LINUX|ANDROID|WIN32|WASM)`), `.config`
(`"debug"`/`"release"`), `.backend`, `.gpu`, `.runtime`, `.arch`. Unset fields are wildcards.

**Properties** take a visibility (`MEL_PUBLIC` propagates to dependents along the graph;
`MEL_PRIVATE` stays on the declaring target), then a `Mel_When`, then a NULL-terminated string list:

- `mel_includes(t, vis, when, "include", …)` — paths are resolved relative to the target's dir
  (an absolute `/…` is taken verbatim) and emitted as `-I`.
- `mel_defines(t, vis, when, "FOO=1", …)` → `-D`.
- `mel_cflags(t, vis, when, "-fobjc-arc", …)` → verbatim.
- `mel_link(t, vis, when, "-lbcrypt", …)` → verbatim link flags.

**Sources** are globs relative to the target dir, with `*` (one segment) and `**` (recursive):

- `mel_sources(t, when, "src/*.c", "src/metal/*.m", …)`
- `mel_exclude_source(t, when, "src/legacy/*.c", …)` removes already-gathered matches.

There is no automatic axis-directory selection: to build `src/win32/` only on win32, you gate it
with `WHEN(.platforms = MEL_ON(WIN32))` yourself. Conventional layout is `src/<platform>/`,
`src/<gpu>/`, `src/<backend>/`, `src/<runtime>/`, but the gating is always explicit.

**Dependencies & misc.**

- `mel_depends(t, "core")` — link + inherit `MEL_PUBLIC` properties; `mel_depends_host` for a
  host-tool dependency.
- `mel_unavailable(t, WHEN(.platforms = MEL_ON(WASM)))` — declare the target unbuildable on a
  variant; a build that needs it there fails loudly with the reason.
- `mel_manifest(t, "BUNDLE_ID", "orgwall.app")` — key/value consumed by packaging templates.
- `mel_codegen(t, "tool", "out.c", arg, …)` — see Codegen below.
- `mel_cmake(t, dir, "-DFOO=ON", …)` / `mel_configure(t, dir, "--enable-x", …)` — see Third-party.

`api.c` setters are stateless and identical whether `build.c` is compiled into its discovery `.so`
or into `nob`; the same header (`build.h`) is the whole consumer contract.

## Variant & axes

`mel_variant_native(platform, config)` (resolve.c) fixes the per-platform defaults; the resolved
variant applies to the entire dependency closure.

- **platform** — host-detected by default (the runner assumes a macOS host); else the positional
  argument. Canonical names: `macos`, `ios`, `linux`, `android`, `win32`, `wasm`.
- **config** — `debug` (default) or `release`, via `--debug`/`--release` anywhere on the line.
  Adds `-g -O0` vs `-O2 -DNDEBUG`; every TU also gets `-std=c23`.
- **arch** — `--arch=<a>` (`arm64`/`x86_64`; `wasm32` nominal). Per-platform defaults: `arm64`
  on macos/ios/android, `x86_64` on linux/win32.
- **gpu** — defaults `metal` (macos/ios) / `vulkan` (linux/android/win32) / `webgpu` (wasm), and is
  overridable with `--gpu <id>` / `--gpu=<id>`, validated per platform (macos: metal/vulkan/webgpu;
  ios: metal; linux: vulkan; android: vulkan/webgpu; win32: vulkan; wasm: webgpu — an invalid pick
  fails loudly). `build.c` gates the backend's sources and link flags through `WHEN(.gpu = …)`.
- **backend / runtime** — *fixed per platform*, not CLI-selectable. backend
  `cocoa`/`uikit`/`androidnative`/`winui`/`dom` (none on linux); runtime `emscripten` on wasm,
  unset (native) elsewhere. To vary one, change the default in `resolve.c`. (The old
  `platform:backend:runtime` positional suffix is parsed-and-ignored.)

## Verbs

    ./nob build     <target> [platform]   # configure → compile → link → package
    ./nob run       <target> [platform]   # build, then launch (host: exec; android: adb install+start; wasm: serve)
    ./nob debug     <target> [platform]   # build, then lldb (host) / adb logcat (android)
    ./nob test      [name]   [platform]   # build & run each mel_add_test target (optionally one by name)
    ./nob configure <target> [platform]   # emit build.ninja only, no compile
    ./nob compile   <target> [platform]   # build without packaging
    ./nob link      <target> [platform]   # (same as compile — no object-only stop yet)
    ./nob package   <target> [platform]   # build + package
    ./nob compdb    [platform …]          # write root compile_commands.json (see below)

Flags usable anywhere: `--debug`/`--release`, `--gpu=<id>`/`--gpu <id>`, `--arch=<a>`, and
`-- <args>` forwards the remainder to the launched binary (`run`) or test.

## Outputs & layout

Per target: `<target_dir>/build/<platform>-<config>/` — objects under `obj/` (mirroring the source
tree), generated sources under `gen/`, the emitted `build.ninja` at the root target's outdir.

- Library → `lib<name>.a`; executable → `<name>` (`.exe` on win32); the root executable is the
  ninja `default`.
- Host tools build with plain `clang` into `<target_dir>/build/host/`.
- Android executables link as a shared `libmelody.so`; web GUI executables link to `.html` with the
  Emscripten shell.
- Third-party install prefixes land in `<outdir>/prefix/{include,lib}`.

## Toolchains

Selected in `toolchain.c` from the variant; `tc.cross` marks a cross target.

- **macos** — `clang`, `-arch <arch>`. Native; no sysroot flag needed.
- **ios** — `clang -target <arch>-apple-ios13.0 -isysroot $(xcrun --sdk iphoneos --show-sdk-path)`.
- **linux** — `zig cc -target <arch>-linux-gnu` (+ `zig ar`), `-D_GNU_SOURCE`.
- **win32** — `zig cc -target <arch>-windows-gnu` (+ `zig ar`), `.exe`; autotools use
  `<arch>-w64-mingw32-gcc`; resources via `x86_64-w64-mingw32-windres`.
- **android** — the NDK's `clang -target <arch>-linux-android24` and `llvm-ar`, located via
  `$ANDROID_NDK_HOME` or the newest `~/Library/Android/sdk/ndk/*`.
- **wasm** — `emcc`/`emar` (must be on PATH), `.js`/`.html`.

Prerequisites for cross builds: `zig` (linux/win32), an Android NDK (≥ API 24) + SDK
(`$ANDROID_HOME`) and `gradle` for the apk, the Emscripten SDK (`emcc`) for wasm, Xcode command
line tools for ios. For `--gpu vulkan` on macOS, Homebrew `vulkan-loader` + `molten-vk` (the gpu
module adds `-I/opt/homebrew/include -L/opt/homebrew/lib -lvulkan`).

## Third-party

Four integration modes, all keyed off the `build.c`:

- **Amalgamation** — declare the vendored `.c` with `mel_sources`; it compiles like any library
  (e.g. `sqlite3`, `mongoose`).
- **CMake** — `mel_cmake(t, dir, "-DOPT=ON", …)`; `nob` runs configure/build/install into the
  prefix (e.g. `sdl3`).
- **Autotools** — `mel_configure(t, dir, "--enable-x", …)`; `nob` runs `./configure --prefix=…
  --disable-shared` (adding `--host=<triple> CC=…` when cross, and dependency prefixes via
  `CPPFLAGS`/`LDFLAGS`), then `make`/`make install` (e.g. `gmp`, `mpfr`).
- **Prebuilt** — `mel_prebuilt(t, when, url, lib)`; `nob` `curl`s the archive and unzips it into the
  prefix (e.g. `webgpu` downloads the macОS Dawn release).

Each method can be gated to a `Mel_When`: `mel_prebuilt`/its `when`, and `mel_cmake_when(t, when)`
for the cmake step (default `ALWAYS`). A matched prebuilt takes precedence and skips cmake — so a
target can fetch a prebuilt on one platform and build from source on another (webgpu: prebuilt on
macos, cmake-from-source on android, inert when `gpu ≠ webgpu`).

CMake/autotools builds are stamp-gated (`.thirdparty-built`); prebuilt is gated on the expected lib
already being present — a clean rebuild needs `rm -rf` of the target's `build/` dir. On success the
prefix's `include`/`-Llib` (and, off-win32, an absolute `-Wl,-rpath`) are injected as public flags,
so dependents resolve the headers and libraries automatically.

## Codegen

A host-tool plus `mel_codegen` is the one generic codegen primitive. Declare the generator as a
`mel_add_host_tool`, then on the consuming target:

    mel_codegen(t, "continuation-gen", "ticker.gen.c",
                "$dir/example/ticker.cont.h", "$out", "-DMEL_CONT_CODEGEN", "$cflags", "$hostclang");

The tool runs at build time producing `gen/<output>`, which is compiled into the target.
Substitutions in the argument list: `$out` (the generated path), `$dir` (the target's dir),
`$cflags` (the target's compile flags), `$hostclang` (host SDK `-isysroot` + clang builtin-include,
for libclang-based parsers). Header arguments ending in `.h` become order-only build deps
(best-effort; no depfile, so transitive header edits don't retrigger). Two generators ship today:
`enum_str_gen` (in `reflect`) and `continuation_gen` (in `continuation`).

## Packaging

`build`/`package`/`run` package an executable for its platform:

- **macOS / iOS** — a `.app` (`Contents/{MacOS,Resources,Info.plist}`; iOS is flat). `Info.plist.in`
  is `{{KEY}}`-substituted from the target's `mel_manifest` values (with `EXECUTABLE`, `VERSION`,
  `APP_LABEL`, `BUNDLE_ID`, `ICON` defaulted). A `.icns` in the per-app `<platform>/` dir is bundled.
- **win32** — `app.rc` compiled to a resource object, embedding `app.manifest`, version, and an
  `.ico` from `apps/<app>/win32/` if present.
- **android** — copies the `modules/build/android/` gradle template, drops `libmelody.so` into
  `jniLibs/arm64-v8a/`, gathers each dependency's `src/androidnative/java/`, writes
  `gradle.properties` from the manifest, and runs `gradle assembleDebug` →
  `…/outputs/apk/debug/app-debug.apk`.

Templates live in `modules/build/{macos,ios,win32,android}/`; an app overrides any of them by
placing the same file under `apps/<app>/<platform>/`.

## compile_commands.json

`./nob compdb` writes a root `compile_commands.json` (git-ignored) for clangd/LSP. With no
argument it emits a **host-first** set of all six platforms; pass platforms to restrict
(`./nob compdb linux win32`). It covers every `build.c` (so `build.h` and the `mel_*` API resolve),
the build system's own sources, and every target source per platform — flags gathered through the
same `mel_gather_compile` the real build uses, so the DB never drifts. Cross sources carry
`-target <triple>` plus the toolchain's own libc/sysroot headers (scraped from `cc -E -v`), so
non-host files resolve too. It is an explicit step, not auto-run; re-run it after adding files. See
`writeup/2026-05-31-compile-commands-db.md`.

## Known gaps (honest account — MEL-ENGINE-VIII)

- **UI backend and runtime are not CLI-selectable** (gpu is, via `--gpu`). They take the
  per-platform default; the `platform:backend:runtime` positional suffix is parsed-and-ignored.
  Vary them by editing `resolve.c` for now.
- **Native desktop WebGPU (Dawn): macOS proven, Android wired but unverified here.** `--gpu webgpu`
  on macos fetches the Dawn prebuilt and links end-to-end; android builds Dawn from the vendored
  `third-party/webgpu/dawn` source via cmake (mirrors the old path, not re-run this session). Browser
  WebGPU (wasm, emscripten `emdawnwebgpu` port) works.
- **`nob test` runs the discovered `mel_add_test` targets**; there is no synthesized aggregate, and
  most `modules/*/test/*.c` belong to no target (only `continuation` wires tests).
- **`compile`/`link` both mean "build, no package"** — no true object-only stop, and there is no
  `clean` verb (`rm -rf build/` or a target's `build/` dir).
- **DX12 is unimplemented**; win32 GPU has no real backend.
- **Cross GUI gaps** documented in the rewrite writeup: win32/wasm GUI link is blocked on `gmp`
  cross issues; `-dead_strip` is Apple-only; the android native lib name is hardcoded `libmelody.so`.
- The host is assumed to be macOS.
