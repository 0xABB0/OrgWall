# 2026-06-15 — Bazel migration: win32-from-macOS (E1) + slang android/wasm (D1-tail)

Continues the same session as the slang-iOS-framework (D2) recap. Two tracks landed.

## Track E1 — build win32 from a case-insensitive (macOS) host

**Goal.** Let `--config=win32` build from this macOS host; the SDK's CamelCase `Windows.h`
vs source `<windows.h>` on case-insensitive APFS made Bazel's `/showIncludes` validation
reject the headers as undeclared inclusions.

**What landed (commit `8ce4635d`):**
- **`cc_toolchain_config.bzl` — disable dependency discovery.** Replaced the
  `parse_showincludes` feature with `no_dotd_file` (enabled) and dropped `/showIncludes`.
  Bazel then uses the *declared* input set (all transitive hdrs) as the dependency set —
  conservative, correct, case-insensitivity-proof — forgoing only fine-grained
  header-change pruning. (Probed first: dropping `parse_showincludes` alone reverts Bazel
  to GCC-style `.d` files clang-cl never emits → `no_dotd_file` is required too.)
- **`.bazelrc` — real C23.** clang-cl silently drops `/std:c23` (knows only c11/c17/clatest)
  → default C17, no `nullptr`. Forward `/clang:-std=c23` to the clang frontend.
- **`windows/BUILD.bazel` — `ar_files`.** `llvm-lib` is a symlink to `llvm-ar`; the archive
  action staged only the bare tool symlink, so the sandbox got a dangling link (`execvp:
  No such file`). Route the archive action's inputs to the full toolchain filegroup.
- **`mel_win32_resources.bzl` — manifest include path.** `llvm-rc` was given only the
  generated `.rc`'s dir on `/I`, never `app.manifest`'s own package dir → `file not found`.

**Verified:** `//modules/power` (a `<windows.h>` consumer) and `//modules/collection`
(C23 `nullptr`) **compile + archive** from macOS; `hello-world-gui` compiles its GUI TUs +
`llvm-rc` resources. macOS unaffected (only the win32 toolchain/rc-rule/`.bazelrc:win32`
block changed).

**What E1 did NOT solve — the win32 *link* surface (scoped, deferred).** Probing the link
exposed three independent blockers, all pre-existing and larger than E1:
1. **`gmp` (autotools via rules_foreign_cc) fails to cross-compile to win32/MSVC** —
   `./configure` "could not find a working compiler" with clang-cl. Nearly every win32
   binary pulls `math → gmp` (the test framework too), so the link surface is gated behind
   this. Hard foreign-cc work.
2. **System libs are GNU-spelled.** ~30 modules declare Windows libs as `-luser32`/
   `-ldbghelp`/… (portable, correct for ld64/lld/emcc) which **lld-link silently ignores**
   → `undefined symbol: MessageBoxW`. Needs a win32-arm respelling to `user32.lib` etc.
   `lld-link` itself works (it reports the exact missing libs).
3. **`<dinput.h>`-class source/compile gaps** in a few win32 backends.

These are Track F/D (per-platform convergence + foreign-cc), not the case-sensitivity wall.
`lld-link` is sound; the toolchain is correct.

## Track D1-tail — slang android + wasm prebuilts wired

Reused the D2 vendored-zip pattern. Both artifacts are git-tracked in
`tools/build/vendor/slang/`.

**Android (commit `68c47645`).** `libslang-compiler.so` (27 MB) `NEEDs` only
libdl/libm/libc — the sibling glslang/glsl-module `.so`s are `dlopen`'d for GLSL output,
**not `DT_NEEDED`** (unused by the Vulkan/SPIR-V path), so omitted, mirroring the macOS arm
which links only the compiler. A genrule extracts the `.so` + headers; `slang_android`
puts the `.so` in `srcs` (links the app's `libmelody.so` against it; android_binary bundles
it into `lib/arm64-v8a/`); wired into the `slang-runtime` `plat_android` arm.
- **Engine fix:** `gpu/vulkan/surface.c` `#include "linux/surface.h"` was guarded only by
  `#if defined(__linux__)` — Android defines `__linux__` too (but uses the android surface
  path), and the BUILD stages `linux/*.h` only for `plat_linux` → fatal "file not found".
  Guarded with `&& !defined(__ANDROID__)`. (The dispatch already had a correct
  `__ANDROID__`-before-`__linux__` branch; only the header include was wrong.)
- **Verified:** slang wrapper + `//modules/gpu` compile on android; the `ballgame` apk link
  **resolves every slang symbol** against `libslang-compiler.so` (zero slang undefineds).

**wasm (commit `68c47645`).** Fully static: 6 archives (`libslang-compiler.a` +
core/compiler-core/cmark/lz4/miniz). A genrule extracts them + headers; `slang_wasm` lists
all six in `srcs` (wasm-ld resolves archives iteratively — no single-pass ordering trap);
wired into the `plat_wasm` arm. Added **`-fwasm-exceptions`** to the wasm link: the archives
use native wasm EH, and emcc otherwise links the `-noexcept` libc++abi → undefined
`__cpp_exception`/`_Unwind_CallPersonality`.
- **Verified:** slang wrapper compiles (`libslang.a`); a throwaway `cc_binary` linking the
  wrapper **links the full archive chain cleanly** on wasm (built, inspected, removed).

## Kludges & debt (MEL-ENGINE-VIII — confessing all)

- **win32 loses header pruning globally** (not just macOS host). `no_dotd_file` is
  unconditional in the win32 toolchain — case-sensitive hosts (Linux/CI) also forgo the
  `/showIncludes` optimization. Correctness is unaffected (declared inputs are conservative);
  a host-conditional toggle would need exec-filesystem case-sensitivity detection, which
  Bazel doesn't expose. Plan-sanctioned trade-off, but it is a real loss on Linux/CI.
- **win32 `/std:` for C++ unverified.** `/std:c++23` (clang-cl-honored) is set but no win32
  C++ TU has been compiled yet (gmp blocks reaching them).
- **android omits the glslang/glsl-module plugins** → GLSL *output* unavailable on android
  (SPIR-V works). Same documented gap as the macOS slang runtime. The plugins would need
  bundling as apk jniLibs + a runtime search path if GLSL output is ever wanted.
- **android apk does not fully build** — blocked by `gpu/vulkan` direct-linking Vulkan 1.1/1.2
  symbols (`vkGetBufferDeviceAddress`, `vkGetPhysicalDeviceMemoryProperties2`) that android's
  API-26 `libvulkan.so` does not export (they require `vkGet*ProcAddr` dispatch / volk). A
  separate gpu-android engine task; **unrelated to slang** (every slang symbol resolved).
- **wasm slang needs wasm-EH everywhere it's linked.** `-fwasm-exceptions` propagates to any
  wasm binary linking slang. The `slang-compile` *test* can't link it: `tools/test/runner`
  uses setjmp/longjmp and was compiled for legacy emscripten SjLj (`emscripten_longjmp`),
  which conflicts with wasm-EH; a real wasm+slang app would need `-sSUPPORT_LONGJMP=wasm` at
  **compile** time across the SjLj users. Orthogonal to slang; flagged for the wasm app track.
- **slang headers vendored 3× on the device platforms** (`ios_include`/`android_include`/
  `wasm_include`) — identical bytes, each self-contained from its own zip (matches the
  per-platform http_archive pattern for macos/linux/win32).

## Suggestions

- **wasm GPU remains an engine-backend effort**, unchanged: `display` has no wasm axis, and a
  wasm GPU app additionally needs `-sASYNCIFY=2` + the SjLj-mode reconciliation above. slang
  is no longer the blocker there.
- **android GPU** next-unblock is the Vulkan 1.1+ symbol dispatch (volk or manual
  `vkGetDeviceProcAddr`) — then the `ballgame`/`hello-gpu` apks build with slang already wired.
- The **win32 link column** is gated on `gmp`→MSVC (foreign-cc) first; the `-l`→`.lib`
  respelling and `<dinput.h>` gaps follow. Track it as Track F, not E1.
