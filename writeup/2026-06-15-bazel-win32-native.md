# 2026-06-15 — Native Windows Bazel build (`--config=win32native`)

## Work done — what changed, and why

Goal: build the repo **natively on the Windows box** (Bazel runs on Windows), not
cross from macOS. Bazel was not installed.

- **Bazel install.** Dropped `bazelisk` at `C:\Users\Gabbo\bin\bazel.exe` (added to user
  PATH). It auto-fetches Bazel `8.7.0` per `.bazelversion`. No admin needed.

- **Root cause of the win32 toolchain not working natively.** `bazel/toolchain/windows`
  is cross-only by construction. `toolchains_llvm` **hard-refuses to run on a Windows
  host** (`repo.bzl`: `if os == "windows": rctx.file("BUILD.bazel"); return` → empty
  `@llvm_toolchain_llvm`), and `@xwin`'s splat shells out to unix `mv`. So the hermetic
  clang-cl/lld-link/llvm-lib + xwin sysroot path can only populate from macOS/Linux.

- **Native toolchain (chosen approach: Bazel's built-in clang-cl).** Added
  `--config=win32native` in `.bazelrc` that drives Bazel's autodetected clang-cl
  toolchain over the **locally installed** LLVM (`C:\Program Files\LLVM`, clang 19.1.1)
  + the installed Windows SDK (10.0.22621) / MSVC CRT (14.43):
  - `--repo_env=BAZEL_LLVM=C:/PROGRA~1/LLVM` (8.3 short path + forward slashes — rc
    tokenization eats both spaces and backslashes in the value).
  - New exec platform `//bazel/platforms:win32_clang_cl_host` carrying
    `@rules_cc//cc/private/toolchain:clang-cl` (the clang-cl toolchain gates its
    `exec_compatible_with` on it). Passed via `--extra_execution_platforms`.
  - `--extra_toolchains=@@rules_cc++cc_configure_extension+local_config_cc//:cc-toolchain-x64_windows-clang-cl`
    — outranks the root-module custom cross toolchain without unregistering it, so the
    macOS cross path is untouched.
  - Windows API baseline defines `/D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00
    /DNTDDI_VERSION=0x0A000001`; otherwise the SDK hides modern entry points
    (`GetDpiForWindow`, `GetSystemTimePreciseAsFileTime`).

- **Host-aware llvm-rc.** New repo rule `//bazel/toolchain/windows:llvm_rc.bzl`
  (`@mel_win32_rc`): junctions the local LLVM `bin` on a Windows host (junction = no
  privilege) and exposes `llvm-rc`; aliases to the hermetic `@llvm_toolchain_llvm` on a
  unix host. `mel_win32_resources` now defaults its `_llvm_rc`/`_llvm_rc_files` to it
  instead of the (empty-on-Windows) hermetic repo. Wired via `use_repo_rule` in
  `MODULE.bazel`.

- **Linkopt respelling (`-lfoo` → `foo.lib`).** lld-link ignores GNU `-l` spelling →
  undefined symbols. Respelled every `plat_win32` linkopt across 22 module BUILDs plus
  the win32-only `gpu_d3d12` target. Scoped strictly to `plat_win32` branches; no other
  platform touched. This is also the repo's known "GNU→MSVC system-lib respelling" cross
  blocker, so it should help the macOS cross link too.

- **`vat` message-pump libs.** `modules/vat/win32/src/waiter_msg.c` calls user32 APIs
  (`PeekMessageW`, `MsgWaitForMultipleObjectsEx`, …) but declared no win32 lib. Added
  `user32.lib` to its `plat_win32` linkopts. (This is why GUI-less apps like hello-net
  failed to link.)

## Result

`bazel build //apps/<app> --config=win32native` builds **and runs** natively:
- **hello-window** — verified: compiles, links, the `.exe` launches (MZ PE, window shown).
- **hello-net**, **geo-tour** — build clean.

3/18 native app binaries build. The toolchain itself is proven end-to-end.

## Kludges / debt (MEL-ENGINE-VIII — confess all)

- **Canonical repo label in `.bazelrc`.** `--extra_toolchains` points at the mangled
  `@@rules_cc++cc_configure_extension+local_config_cc//:cc-toolchain-x64_windows-clang-cl`.
  There is no clean apparent name (the cc autoconf is used by rules_cc, not the root
  module). Stable for this module graph but ugly and could drift if the dep structure
  of rules_cc changes.
- **8.3 short path + forward slashes** for `BAZEL_LLVM` (`C:/PROGRA~1/LLVM`) to dodge rc
  tokenization eating spaces/backslashes. Brittle if LLVM is installed elsewhere — the
  path is hardcoded to `C:\Program Files\LLVM`.
- **`@mel_win32_rc` fails loudly if `BAZEL_LLVM` is unset on a Windows host.** By design
  (no silent default, MEL-CODE-007), but it means a plain `--config=win32` on Windows (no
  `native`) gives a confusing error rather than a "use win32native" hint.
- **`win32native` is a parallel config to `win32`,** duplicating platform/gpu/std lines.
  Not DRY; a shared anchor would be cleaner but I avoided touching the proven `win32`
  block.
- Could not test the macOS **cross** path from here. The shared edits (linkopt respell,
  `mel_win32_resources` → `@mel_win32_rc` alias) are additive and believed equivalent or
  better for cross, but unverified.

## Remaining blockers (pre-existing WIP, shared with the cross build)

1. **`gmp`/`mpfr` via `rules_foreign_cc` — the dominant gate (~14 apps).**
   `modules/math` → `//third-party/mpfr` → `//third-party/gmp`, both `configure_make`.
   On a Windows host `rules_foreign_cc` bootstraps GNU Make through WSL's
   `C:\WINDOWS\system32\bash.exe` and fails; even past that, gmp's autotools/GNU-asm
   configure does not target the MSVC ABI. This is the CLAUDE.md "gmp→MSVC via
   rules_foreign_cc" item. Solving it (prebuilt gmp/mpfr, or an MSVC-native bignum) is
   the single highest-leverage next step — it unblocks `math` and nearly every app.
2. **Latent win32 source bugs** surfaced once compilation got this far, e.g.
   `modules/input/win32/src/input_win32.c:613` uses `.relative`, not a field of
   `Mel_Input_Mouse_Event`. These would fail the cross build too.
3. **`vulkan-1.lib`** (gpu module, gpu apps) — the Vulkan loader import lib isn't on the
   SDK lib path; needs the loader stub/loader wired for native.
4. **`repl-cli`, `hello-async`** are gated `target_compatible_with` incompatible on win32.
5. **`midi-monitor`** hit a transient "couldn't delete action output directory" (Windows
   file lock) — retryable, not a real failure.

## Update — gmp/mpfr native build (the headline blocker, now cleared)

Reproduced nob's recipe as a repository rule `//bazel/rules:gmp_mpfr_win32.bzl`
(`@gmp_mpfr_win32`): on a Windows host it copies the gmp + mpfr source trees in
(`cp -rp`, preserving mtimes) and runs `sh ./configure && make && make install`
with the plain **`clang` driver** (`CC='clang -std=gnu17'`, MSVC ABI),
`AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm LD=ld.lld`, gmp `--disable-assembly`,
then mirrors `lib<x>.a` → `<x>.lib` **after each install** (mpfr's configure link
test needs `gmp.lib` present). Paths handed to clang stay Windows-style (`D:/...`).
Exposes `:gmp` and `:mpfr` `cc_library`s. Build runs at repo-fetch (~4 min once).

Wiring:
- New `bool_flag //bazel/config:win32_native_host` + `config_setting :native_win32`;
  `win32native` sets it `=True`. Cleanly separates native from the macOS→win32 cross
  build (same target platform).
- `//third-party/gmp:gmp` and `//third-party/mpfr:mpfr` are now `alias`es selecting
  `@gmp_mpfr_win32//...` under `:native_win32`, else the original `configure_make`
  (renamed `:gmp_foreign` / `:mpfr_foreign`). The cross path is untouched.

Gotchas solved: `make` tried to regenerate shipped `aclocal.m4`/`configure`/`*.info`
via aclocal/automake/makeinfo (absent) because git-checkout + copy skews mtimes — fixed
with `cp -rp` plus `MAKEINFO/ACLOCAL/AUTOMAKE/AUTOCONF/AUTOHEADER=true` (no-op the
maintainer-mode rules).

**Result: native app binaries went 3 → 8.** `hello-world-gui` (links gmp/mpfr via
`math`) builds and runs. gmp/mpfr is no longer a blocker for any app.

### New kludges

- `@gmp_mpfr_win32` hardcodes MSYS2 at `C:\tools\msys64` and builds at repo-fetch time
  (outside the action graph, like nob — no per-file caching, rebuilt on `clean
  --expunge`). It does not re-run when the gmp/mpfr sources change (only depends on their
  `BUILD.bazel` label), so a source bump needs a manual expunge.
- `cp -rp` of both full source trees per fetch (~tens of MB); fine but not free.

### Remaining per-app failures after the unblock (varied, NOT gmp/mpfr)

`RegGetValueW` undefined (barcode-reader, camera-*) → a module needs `advapi32.lib`;
`clang_createIndex` undefined (display-gui, melody-showcase) → the `clang`/libclang
module needs the libclang import lib; `vulkan-1`/loader (ballgame, hello-gpu); plus WIP
source-compile bugs in `musictheory` (midi-monitor, music-companion), hello-gpu,
compress-lab, hello-speech. These are independent of the gmp/mpfr work.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document the native path: `--config=win32native` builds on the Windows box itself
  using locally installed LLVM + Windows SDK (no toolchains_llvm/xwin), complementing the
  existing "win32 cross-compiles from macOS" note. Call out the `BAZEL_LLVM` requirement
  and `C:\Program Files\LLVM` assumption.
- Update the win32 WIP blocker list: GNU→MSVC system-lib respelling is now done
  (`foo.lib` in BUILD linkopts); gmp/mpfr foreign_cc remains the headline blocker.

## Suggestions (direction / hygiene)

- **Kill the gmp/mpfr foreign_cc dependency for win32** (and probably everywhere): ship a
  prebuilt static `libgmp`/`libmpfr` per target, or vendor an MSVC-friendly bignum. It is
  the chokepoint for the entire native build and a long-standing cross blocker.
- Consider giving the custom cross toolchain an `exec_compatible_with` that excludes
  Windows hosts, so a native build could rely on plain registration order instead of the
  `--extra_toolchains` canonical-label kludge.
- Root has stale nob artifacts (`nob.exe/.exp/.ilk/.lib/.pdb`, `build/`) post nob-removal;
  worth gitignoring/cleaning.
