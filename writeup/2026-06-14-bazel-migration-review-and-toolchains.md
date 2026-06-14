# 2026-06-14 — Bazel migration: review, hardening, hermetic toolchains

## Work done — what changed, and why

Reviewed the two agents' Bazel port, then executed the fixes and a toolchain rebuild.

**① Single source of truth for platform axes.** `bazel/config/BUILD.bazel` rewritten so
`plat_*`/`arch_*`/`backend_*` derive from `@platforms` constraints; deleted the redundant
`platform`/`arch`/`backend`/`runtime`/`config` `string_flag`s (only `gpu` remains a flag — the
one genuinely free axis). `.bazelrc` reduced to `--platforms` + `--//bazel/config:gpu` per
platform; debug/release now ride native `-c dbg/opt`. All 57 `plat_*` call sites unchanged.
Fixes the silent-default landmine (bare build on Linux selected macOS sources) — MEL-CODE-007.

**Bazel 8.7.0.** `.bazelversion` bumped; `platforms`/`rules_cc` pinned explicitly; skylib→1.9.0.
Bazel-8 `--incompatible_disallow_empty_glob` broke 115 axis globs across 35 files
(`glob(["x/*.c","x/*.h"])` with no `.h`) — fixed with `allow_empty = True` (not by re-disabling
the flag).

**apple_support before rules_cc** in MODULE.bazel — registration order is load-bearing; rules_cc
first silently shadowed the Apple toolchain and broke every `objc_library`.

**NDK de-hardcoded.** Dropped the absolute `/Users/gabbo/.../ndk` path; NDK is located via
`$ANDROID_NDK_HOME`, and Android toolchain registration is deferred to `--config=android`
(`--extra_toolchains`) so macOS/Linux/wasm never fetch it.

**Hermetic LLVM.** `bazel/extensions/llvm.bzl` no longer symlinks Homebrew; it `download_and_extract`s
the sha256-pinned `LLVM-22.1.6-macOS-ARM64.tar.xz` and exposes `libclang`/`llvm_runtime` (matching
the host's clang 22). Codegen path verified (`//modules/coro:coro-example`). `no-sandbox`→`local`
on `mel_codegen` (kills shared-cache poisoning, keeps local incrementality).

**Deleted** the dead `bazel/spike/` (referenced a non-existent `mel_codegen` API).

**Toolchain architecture (post-decisions):** reflection deferred (clang 22 has no P2996 — verified),
REPL shelved (`third-party/llvm:llvm-runtime` marked incompatible; the official LLVM release links a
JIT only under lld-22, which Apple ld can't provide and macOS keeps for objc). Resulting per-platform
toolchains: macOS/iOS = apple_support (host Xcode SDK), Linux = zig (hermetic, verified cross-build
from macOS), win32 = clang-cl + xwin (MSVC ABI, below).

**win32 = hermetic clang-cl/xwin (MSVC ABI).** New `bazel/toolchain/windows/`: a lazily-fetched
`xwin_sysroot` repo rule (splats the MS CRT+SDK, renames "Windows Kits"→"WindowsKits"), and a custom
`cc_toolchain_config` (clang-cl/lld-link/llvm-lib from `@llvm_toolchain_llvm`, explicit `/imsvc`+`/LIBPATH`
since clang's `/winsysroot` auto-detect breaks cross-host). Verified: clang-cl produces a valid
amd64 COFF MSVC-ABI object from `<windows.h>`. zig's Windows (MinGW ABI) toolchain dropped.

Verified green: full macOS native build (objc GUI/GPU + the showcase app); Linux + win32 cross-builds
of pure modules from the macOS host (zig Linux; clang-cl compiles win32 objects).

## Kludges — every shortcut and the debt it leaves

- **win32 from a case-insensitive macOS volume does not pass Bazel's `/showIncludes` validation.**
  The SDK ships `Windows.h`; code includes `<windows.h>`; Bazel validates case-sensitively; xwin's
  disambiguating symlinks can't exist on case-insensitive APFS. The *compile is correct on every host*
  (proven: COFF object produced) — only validation differs. Works from Linux/CI/case-sensitive volume.
  Not a defect, but a real macOS-host limitation, documented in the toolchain config.
- **xwin prebuilt fetch (linux/windows exec hosts) is unpinned** (`download_and_extract` without sha256).
  macOS uses PATH xwin so it's untested for linux/win here. Debt: pin sha256 per prebuilt.
- **macOS Vulkan host path remains** (`modules/gpu/BUILD.bazel:91,95` raw `-I/-L/opt/homebrew`). Non-default
  backend (Metal is macOS default); not yet pinned (would mirror slang: pin MoltenVK + loader).
- **`/std:clatest`** (latest C) rather than a pinned c23; the global `--conlyopt=-std=c2x` is ignored by
  clang-cl (suppressed with `-Wno-unknown-argument`). Debt: make std per-platform in `.bazelrc`.
- **toolchains_llvm pulls a 2nd LLVM-22 extraction on macOS** (same sha → download cached, disk dup only).
  Resolves itself when the codegen `@llvm` extension is removed with coro/reflect.
- **REPL/JIT subsystem shelved**, not solved — `apps/repl` + `modules/{llvm,jit,clang,repl}` are paused
  behind a `target_compatible_with` incompatible. Revisit when the C++ direction settles.

## CLAUDE.md suggestions (recommendations only — not applied)

- Add a short "Bazel build" section mirroring the nob commands: `bazel build //... --config=<platform>`,
  the per-platform `--config` names, and the `$ANDROID_NDK_HOME` / win32 case-sensitivity / `cargo install
  xwin` host prerequisites.
- Note that win32 should be built from Linux/CI (or a case-sensitive macOS volume), and that macOS/iOS
  require host Xcode (irreducible Apple SDK).

## Suggestions

- Pin the xwin prebuilt sha256s and add a CI matrix (ubuntu/windows/macos) — Linux is the cleanest
  clone+build; it would also validate win32 cross-builds end-to-end (case-sensitive → no validation wall).
- Pin macOS Vulkan (MoltenVK + Vulkan-Loader) to close the last host-path leak.
- When coro/reflect are deprecated, delete the `@llvm` codegen extension + `mel_codegen` (leaves
  toolchains_llvm as the sole LLVM provider — no duplication).
- Consider a thin `mel_axis_srcs` symbolic macro for the stacked single-key `select()`s in modules like
  `gui` (typo surface), once on Bazel 8 symbolic macros.
