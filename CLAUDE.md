# Melody

An application framework for games, GUI applications, and CLI applications. Never treat this repo as a game engine; being a game engine is a small fraction of what this framework affords.

## Build

Bazel is the build system (version `8.7.0`, pinned in `.bazelversion`; use `bazelisk` or a matching `bazel`). `MODULE.bazel` declares external dependencies; per-platform and per-config flag groups live in `.bazelrc`. Every `modules/<m>/`, `apps/<app>/` and `third-party/<lib>/` carries a `BUILD.bazel` declaring its targets. Invoke from the repo root only.

## Build commands

    bazel build //apps/<app>  --config=<platform>   # compile + link (+ bundle)
    bazel run   //apps/<app>  --config=<platform>   # build then launch/serve
    bazel test  //...         --config=<platform>   # build & run the cc_test targets
    bazel run   //:refresh_compile_commands         # write compile_commands.json for clangd

Target platform and configuration are `--config` groups defined in `.bazelrc`, combined freely:

    --config=macos|ios|linux|android|win32|wasm   # target platform (and its default GPU backend)
    --config=debug | --config=release             # -c dbg (asserts kept, MEL-ENGINE-VIII) | -c opt; debug is the default

The GPU backend is the one axis a platform's config does not fix; override it explicitly:

    --//bazel/config:gpu=metal|vulkan|webgpu      # validated per platform

`metal` (macos/ios), `vulkan` (macos/linux/android/win32), `webgpu` (macos/android/wasm); each
platform's `.bazelrc` block sets its default. Build outputs land under `bazel-bin/`.

It is extremely important to not break the targets on other platforms; be always mindful of the includes you use and what is available on the platform.

### Runnable targets and bundles
A bare `cc_binary` (`//apps/<app>:<app>`) runs on the host via `bazel run`. Packaged bundles are separate targets in the same `BUILD.bazel`: `:<app>_app` (`macos_application`), `:<app>_ios` (`ios_application`), an `android_binary` (apk), and `mel_wasm_app` (wasm — `bazel run` serves it). On the Darwin host, android and ios run on their respective simulators and web in a headless browser.

## Sources & modules

A module is a `cc_library` in its `BUILD.bazel`. It exposes public headers with `hdrs = glob(["include/**/*.h"])` and `includes = ["include"]`; dependents add `deps = ["//modules/<m>"]` and include as `<subtree/...>` (e.g. `<rng/pcg32.h>`), never by module name. Common sources go in `srcs = glob(["src/*.c"])`.

Platform/axis-specific sources, `linkopts` and `deps` are gated **explicitly** with `select()` over the `//bazel/config:plat_<platform>` keys — there is no automatic axis-directory discovery:

    srcs = glob(["src/*.c"]) + select({
        "//bazel/config:plat_macos": glob(["quartz/src/*.c"]),
        "//bazel/config:plat_win32": glob(["gdi/src/*.c"]),
        "//bazel/config:plat_wasm":  glob(["dom/src/*.c"]),
        "//conditions:default": [],
    }),

Conventional layout splits common code from axis-specific code at the module root:

    <m>/
      BUILD.bazel
      include/<m>/        # common public headers, consumed as <m/...>
      src/                # common sources + internal headers
      <axis>/src/         # one folder per platform/gpu axis (quartz, gdi, dom, android, …)
      [manifests, java, …]  # axis resources (AndroidManifest.xml, java/)

An app declares a `:lib` `cc_library` plus a `cc_binary` and any bundle rules (`macos_application`, `ios_application`, `android_binary`, `mel_wasm_app`). A test is a `cc_test` depending on `//tools/test:runner`, `//modules/test`, and the library under test. Custom rules live in `//bazel/rules` (`mel_codegen`, `mel_wasm_app`, win32 resources via `mel_win32_resources.bzl`).

Inside a module folder we have:
- readme.md # concise information about the module (why it exists, what dependencies)
- spec.md # optional, to use when iterating over that module: specification to follow
- todo.md # optional, list of things to fix/add
- BUILD.bazel # build targets
- [optional files as needed]

There is no hard-constraint on how a module is laid out physically. use the structure that fits the most with what the module wants to do; Bazel imposes only the target graph, not a directory shape.

## Tools

Search with `ast-grep`; the source tree's structure (axis dirs, suffixes) is better matched structurally than by line grep.

## Commandments
Development in this repo is bound by the Ten Commandments of the Engine. Honor every one. When a decision turns on one, cite it by tag (MEL-ENGINE-N).

@docs/commandments.md

There are also rules for how to write code in the style of this repo. Never break these rules.

@docs/coding-guidelines.md

## Session writeup
At each session's close, write a recap to `writeup/`, one file per session, named `YYYY-MM-DD-<topic>.md`:
- Work done — what changed, and why.
- Kludges — every shortcut and the debt it leaves; confess all, sanctioned or not (MEL-ENGINE-VIII). The bar is zero; concealment is the only failure.
- CLAUDE.md suggestions — proposed changes to this file, recorded as recommendations only. Never edit CLAUDE.md as part of the recap.
- Suggestions — feature direction, and repo hygiene (code included).

## Behaviour
- This repo is heavily WIP. it's small enough that pervasive changes are doable since consumers are few.
- This repo keeps being worked on, by multiple agents at a time, so if something changes that you did not do, it's fine.
- Use worktrees when working, then merge into main

## Windows builds

`win32` cross-compiles from the macOS host with `--config=win32` (clang-cl + LLVM toolchain under `//bazel/toolchain/windows`). Compile + archive are proven from this host; the link surface is still WIP (gmp→MSVC via rules_foreign_cc, GNU→MSVC system-lib respelling, a few `<dinput.h>` gaps) — consult the latest `writeup/*bazel-win32*` for the live blocker list.

A networked Windows box also exists for native builds over SSH:

- **Host.** SSH alias `win-pilot` (`~/.ssh/config`: `gabbo-windows-desktop`, `100.120.188.120`, user `gabbo`, key `id_ed25519_winpilot`).
- **Checkout.** `D:\repo\OrgWall` (repo root; no nested `melody/`), shares `origin`.
- **Build.** Bare SSH `cmd` lacks the MSVC/clang env; prefix with `C:\Users\Gabbo\dev.cmd` (loads vcvars64 + LLVM, execs the rest):

      ssh win-pilot "cd /d D:\repo\OrgWall && git pull --ff-only && C:\Users\Gabbo\dev.cmd bazel build //apps/<app> --config=win32"

  Unverified that `bazel`/`bazelisk` is installed on the box — confirm before relying on this path.
- **Workflow.** Commit+push here, `git pull`+build there; `origin` is source of truth.
