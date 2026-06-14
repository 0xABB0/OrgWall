# Bazel migration — hand-authored BUILD files, orthogonal axes

## Work done

Replaced the entire machine-generated Bazel layer with hand-authored BUILD.bazel files,
and killed the generator that produced it.

- **Deleted the autogeneration path.** Removed `tools/bazelgen/gen.py`, `modules/build/dump.c`,
  and reverted the `dump` verb wired into `nob.c` / `modules/build/driver.c`. nob is pristine
  again; there is no longer a generator and no `# Generated … Do not edit` files.
- **Rewrote `//bazel/config` with orthogonal axes.** `platform`, `gpu`, `backend`, `runtime`,
  `config`, `arch` are independent `string_flag`s, each value an atomic `config_setting`. Added
  `backend` and `runtime` as first-class axes (the generator had collapsed GUI-backend→platform
  and runtime→platform). No pre-baked combination groups; targets that need a conjunction declare
  a locally-named `config_setting_group`.
- **Hand-authored 120 BUILD files** (modules, apps, third-party) to a single idiom:
  `package(default_visibility=public)`, no comments, short dep labels (`//modules/core`), public
  headers `glob(["include/**/*.h"])` + `includes=["include"]`, private headers beside sources,
  one `select()` per axis attribute with a meaningful `//conditions:default`, `.m` split into a
  sibling `objc_library` gated by `target_compatible_with`. Dependents lean on Bazel's transitive
  propagation of hdrs/includes/linkopts instead of restating frameworks and grandparent deps.
- **Per-backend GPU libraries** (your steer). `//modules/gpu:gpu` selects one of
  `gpu_metal` / `gpu_vulkan` / `gpu_webgpu` / `gpu_d3d12` on the `gpu` axis; verified
  `--//bazel/config:gpu=vulkan` flips `:gpu`'s dep to `:gpu_vulkan`, `=webgpu`→`:gpu_webgpu`,
  default→`:gpu_metal`. Each backend is its own focused (objc-)library; macOS-only vulkan/webgpu
  surface `.m` are further split and made incompatible unless `gpu=X AND plat_macos`.
- **Portable `_core` sub-libraries for audio.** The `audio*` "-core" tests run hardware-free by
  recompiling only the portable sources; nob did this by borrowing sibling source files. Replaced
  that with `audioout_core` / `audioin_core` / `audiopolicy_core` / `audioplayback_core`
  cc_libraries that the tests (and cross-module borrowers like `stt`, `audiomixer-device`) depend
  on — no source-file borrowing, no `_hdrs`/`exports_files` hacks.
- **Codegen** via the existing `mel_codegen` rule (`coro`, `musictheory`, `musicnotation`,
  `display`, `gamepad`, `input`, `compress-lab`); `coro`/`reflect` expose their libclang host
  tools (`coro-gen`, `enum-str-gen`) as `cc_binary` against `@llvm//:libclang`.
- **Killed the generator crutches**: every `_hdrs` filegroup, `_embed` catch-all, and
  `exports_files` line is gone; degenerate `glob(["src/a.c","src/b.c"])` over named files became
  plain lists or honest `glob(["src/*.c"])`; `#embed` is staged via a scoped `textual_hdrs` lib
  (`//apps/hello-gpu:assets` = `shaders/**`) rather than "every non-source file".

**Result:** 318 of 319 targets build green on the macOS host; 92 of 105 tests pass. The lone
non-building target is a pre-existing stale-source bug; the 13 failing tests are one third-party
runtime gap (below). The migration was driven through two background workflows (89 + 10 packages
authored in parallel) plus hand-authoring of every bespoke case, each verified by `bazel build`.

## Kludges & debt (MEL-ENGINE-VIII — confessing all)

- **`coro` golden/reject tests omitted (9 targets).** They hardcode nob's output path
  (`-DCORO_GEN=modules/coro/build/host/coro-gen`) and were left unwired (`BAZELGEN-TODO`) even in
  the generated layer, so omission is no regression — but it is lost coverage. Proper port: a
  `data=[":coro-gen"]` runfiles dep + a runfiles-relative `CORO_GEN`, which needs a touch of
  `golden.c`/`reject.c` source change.
- **slang runtime plugin dylibs not staged → 13 tests fail at runtime** (all `//modules/gpu:*`,
  `//modules/image:image-gpu-test`, `//third-party/slang:slang-compile`). They build and link;
  at runtime `libslang.dylib` loads `@rpath/libslang-compiler.dylib` etc. which Bazel doesn't
  co-locate. The siblings include non-linkable plugin modules (`libslang-glsl-module`,
  `libslang-glslang`, `libslang-llvm`) — globbing them into `srcs` breaks the link. Needs a real
  staging rule (runfiles + rpath, or a custom `cc_import`-style rule). Left `slang.BUILD` linking
  only `libslang.dylib` so the graph stays green.
- **Android Java/manifest dropped repo-wide.** Every `mel_android_java` / `mel_android_manifest`
  has no Bazel representation (consistent with the generated layer). Android packaging is
  unrepresented.
- **Foreign third-party are clean stubs, not builds.** `sdl3` (cmake; currently depended on by
  nothing), `webgpu` (Dawn prebuilt — links `-lwebgpu_dawn` on macos/android webgpu but assumes
  the lib is present; not fetched via Bazel), `vulkan-loader-stub` (empty; nob generated a stub
  `.so` at configure time — unported). All Phase-4 `rules_foreign_cc`/`http_archive` work.
- **Cross-platform fidelity unverified.** Only the macOS host (metal/cocoa) is exercised. All
  win32/linux/android/wasm/ios sources, frameworks and backends are authored faithfully from
  `build.c` but not built — only `linux` toolchains are registered, and that path is untested here.
- **Private includes leak.** `musictheory`/`musicnotation` keep their `MEL_PRIVATE`
  `include/<m>` dir in `includes` (so it propagates) rather than as a truly-private `-I`.
- **Public-by-default visibility everywhere.** nob had no visibility model; tightening is a
  separate pass.

## Not my debt, but flagged

- **`apps/music-companion` does not compile.** `companion.c` calls
  `mel_audiocapture_default_device` (declared nowhere in the repo) and passes a `u32` where a
  `Mel_AudioIn` is expected. Pre-existing source/API drift — nob would fail it too. Excluded from
  the green set; the app owner must update the source.
- **Pre-existing third-party BUILDs carry comments** (`gmp`, `mpfr`, `llvm`, `lz4`, `slang`),
  authored by an earlier session and skipped by the generator. Left untouched (their comments
  explain real autotools/toolchain friction), but inconsistent with the no-comment idiom.

## Multi-platform bring-up (toward killing nob)

- **Linux toolchain fixed.** The `single_version_override` `patch_cmds` that was meant to bump
  zig 0.12→0.14 (LLVM 17→19, for C23 `constexpr`) never applied — the repo cached unpatched and
  `VERSION` stayed `0.12.0`. Replaced it with real `.patch` files (`bazel/patches/zig_0_14_*.patch`,
  `patch_strip=1`): `zig_sdk.bzl` (VERSION + 5 sha256 + `os-arch`→`arch-os` naming), `defs.bzl`
  (`dl_platform`), and — newly needed — `zig-wrapper.zig` for zig-0.14 std changes
  (`ComptimeStringMap`→`StaticStringMap`, `std.ChildProcess`→`std.process.Child`,
  `mem.split`→`mem.splitScalar`). Linux now compiles with C23 `constexpr`.
- **All six platforms wired.** `.bazelrc` sets every axis per platform (matching `resolve.c`:
  platform/backend/gpu/runtime/arch); `//bazel/platforms` defines all six; added a `none` backend
  (Linux has no GUI backend); registered the win32 zig toolchain. macOS host unchanged (318 green).
- **Five clang-family toolchains live and verified** (portable modules build on each):
  macOS (apple_support, 318 green), iOS (apple_support), Android (NDK 29 / clang 20, api_level 26,
  via `rules_android_ndk`), wasm (emscripten via the `emsdk` 4.0.17 module), Linux (zig 0.14).
  Android also builds the audio/GUI backends after the fixes below. win32 zig-mingw is registered
  but blocked: its toolchain doesn't declare `any-windows-any` headers as builtin (Bazel rejects
  `Windows.h` as an undeclared inclusion), and the win32 code is MSVC-targeted (nob builds it on a
  remote Windows box, not mingw).
- **Axis private-header staging fixed (109 globs).** Backends globbed `<axis>/src/*.c` without the
  sibling `*.h`; the sandbox stages only declared inputs, so cross-builds failed with e.g.
  `audioout_android_internal.h not found`. Swept every `<axis>/src/*.c` glob to also stage
  `<axis>/src/*.h`. macOS host unaffected (still 318 green).

### Foreign-cc autotools cross — fixed

`gmp`/`mpfr` (and thus `math`) now cross-compile on **all five** toolchain platforms
(macOS, iOS, Linux, Android, wasm). The `configure_make` targets were native-only; cross needed:
- `--host=<triple>` per platform (so autoconf enters cross mode and stops running its test binaries).
- a `CC_FOR_BUILD` wrapper (`third-party/gmp/cc_for_build.sh`) that forces a native macОS build
  compiler with the macOS SDK and strips the cross `-isysroot`/`-target` that gmp leaks into its
  build-time tool compile — without it, an iOS/Android SDK isysroot poisons the build compiler.
- the Apple libtool-as-archiver workaround (`AR=ar`/`RANLIB=ranlib`) extended from macOS to iOS.
The macОS-host path is unchanged.

### What still blocks full parity / nob deletion (with evidence, not hand-waving)

1. **Engine has no Linux backend for some modules.** `vat` implements waiters only for
   darwin/macos/win32 (`darwin/src/waiter_kqueue.c`, `win32/src/waiter_msg.c`); there is no
   `linux/src` waiter, so anything linking `vat` fails with `undefined symbol: mel_vat_waiter_io`
   on Linux. This is missing **source**, not a BUILD gap — nob would not link it either.
2. **System dev libraries absent from the hermetic libc.** `dialog`/`notification` need
   `dbus/dbus.h`; GUI/audio/gpu need X11/wayland/vulkan/alsa. zig ships libc only; these niche
   `-dev` headers/libs are not in standard sysroots. nob built on an apt-provisioned Linux box.
   Bazel parity needs a custom sysroot that vendors every system dependency.
3. **Host-LLVM coupling.** `clang`/`llvm`/`jit` include `mach-o/dyld.h` and `llvm/.../LLJIT.h`;
   the `@llvm` extension is Homebrew-macOS only. Cross builds need a per-target LLVM.
4. **Android/wasm SDKs are wired** (NDK via `rules_android_ndk`, emscripten via `emsdk`) and build
   the portable + backend modules; what remains is Android Java/manifest packaging and emscripten
   ports. **win32** zig-mingw is registered but blocked (the toolchain doesn't declare
   `any-windows-any` as builtin → `Windows.h` is an undeclared inclusion; and the code is
   MSVC-targeted, which is why nob builds win32 on a remote Windows box).
5. **Packaging + run/debug verbs** (`.app`/`.apk`, simulators) — not ported.

**Therefore nob cannot be deleted yet without breaking the build for 5 of 6 platforms.** That is
sequencing gated on external SDKs/sysroots and on engine code that does not exist yet (Linux vat),
not reluctance. The toolchain layer is now correct; the remaining work is per-platform
provisioning and a handful of missing engine backends.

## CLAUDE.md suggestions (recommendations only)

- Document the Bazel build surface once it reaches parity: the axis flags
  (`--//bazel/config:{platform,gpu,backend,runtime,arch,config}=…`) and the `--config=<platform>`
  shortcuts, mirroring the existing nob command table.
- State the BUILD.bazel idiom (the one above) so future agents and contributors don't reintroduce
  `_hdrs`/`_embed`/degenerate-glob patterns.

## Suggestions

- **Path to "100% kill nob":** the remaining blockers are real capabilities, not BUILD authoring —
  (1) register win32/wasm/ios/android toolchains and verify cross builds; (2) packaging (`.app`/
  `.apk`) as Starlark rules to replace `nob package`; (3) `run`/`debug` wrappers; (4) the
  `rules_foreign_cc` third-party (sdl3/webgpu/dawn/vulkan-loader) and the slang runtime staging;
  (5) Android Java/manifest rules. Once those land, delete `modules/build/`, `nob.c`, and every
  `build.c`.
- **Make slang staging the first follow-up** — it is the only thing between "318 build" and a
  green `bazel test //...` on the host.
- **A drift guard during the transition:** until nob is deleted, a small CI check that diffs each
  target's source/dep set between the two systems would catch a `build.c` edit that forgets its
  BUILD.bazel. (Optional; costs keeping `nob dump` alive, which was just removed — only worth it
  if the transition is long.)
