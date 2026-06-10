# Melody

An application framework for games, GUI applications, and CLI applications. Never treat this repo as a game engine; being a game engine is a small fraction of what this framework affords.

## Build

`nob` is the build system; its full implementation lives in `modules/build/` — which is the framework itself, not a discovered module (it has no `build.c`). Every other target — modules included — carries a `build.c`. Invoke from the repo root only.

Fuller documentation lives in `modules/build/platforms.md`.

## Build commands

    ./nob build <target> [platform]            # configure -> compile -> link (-> package)
    ./nob run   <target> [platform]            # build then launch/serve
    ./nob debug <target> [platform]            # build then run under debugger (Android: tail logcat)
    ./nob test  [name] [platform] [-- <args>]  # build & run the discovered mel_add_test targets
    ./nob package <target> [platform]          # packages the target in a .app/apk
    ./nob configure|compile|link <target> [platform]
    ./nob compdb [platform ...]                # write compile_commands.json for LSP (host-first; all platforms if none)

Configuration, GPU backend and target arch are selectable by flag anywhere on the line:

    --release | --debug     # configuration (default is debug)
    --gpu=<id> | --gpu <id> # GPU backend, validated per platform
    --arch=<arch>           # target architecture (arm64, x86_64)
    --device                # run on device instead of simulator

Platform defaults to the host; otherwise it is the positional argument. Known platforms:
`macos`, `ios`, `linux`, `android`, `win32`, `wasm`. Valid GPU backends: `metal` (macos/ios),
`vulkan` (macos/linux/android/win32), `webgpu` (macos/android/wasm); each platform's default is
the first that applies. The UI backend and runtime are fixed per-platform defaults (not
CLI-selectable). Output lives under each target's `build/<platform>-<config>/`.

### Runnable targets
On the Darwin host, android, ios and web are runnable too. For mobile devices, unless --device is passed, it will run on the respective simulator. For web, you can use a headless browser

## Sources & modules

Every `modules/<m>/`, `apps/<app>/` and `third-party/<lib>/` carries a `build.c` whose `build()` declares its artifacts; discovery runs each one. A module exposes its headers by declaring `mel_includes(t, MEL_PUBLIC, ALWAYS, "include")`; dependents that `mel_depends` on it inherit that path and include as `<subtree/...>` (e.g. `<rng/pcg32.h>`), never by module name. `<m>/src` holds its code.

Within a target, sources and flags are gated **explicitly** by a `Mel_When` selector — there is no automatic axis-directory discovery or basename shadowing. Conventional layout is `src/` (common) plus `src/<platform>/`, `src/<gpu>/`, `src/<backend>/`, `src/<runtime>/`, but each subset is selected by the target's `build.c` (e.g. `mel_sources(t, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c")`). See `modules/build/platforms.md` for the full authoring surface.

Fuller documentation lives in `modules/build/platforms.md`.

Inside a module folder we have:
- readme.md # concise information about the module (why it exists, what dependencies)
- spec.md # optional, to use when iterating over that module: specification to follow
- todo.md # optional, list of things to fix/add
- build.c # build configuration
- [optional files as needed]

There is no hard-constraint on how a module is laid out physically. use the structure that fits the most with what the module wants to do, since the build system is powerful and customizeable.

## Tools

Search with `ast-grep`; the source tree's structure (axis dirs, suffixes) is better matched structurally than by line grep.

## Commandments
Development in this repo is bound by the Ten Commandments of the Engine. Honor every one. When a decision turns on one, cite it by tag (MEL-ENGINE-N).

@docs/commandments.md

There are also rules for how to write code in the style of this repo. Never break these rules.

@docs/coding-guidelines.md

## New features
When developing new features, follow this workflow:
- Write a design spec in `design/` (freeform).
- Iterate it against every failure mode.
- Split it into more granular specs.
- Implement those with no prerequisite first.

## Session writeup
At each session's close, write a recap to `writeup/`, one file per session, named `YYYY-MM-DD-<topic>.md`:
- Work done — what changed, and why.
- Kludges — every shortcut and the debt it leaves; confess all, sanctioned or not (MEL-ENGINE-VIII). The bar is zero; concealment is the only failure.
- CLAUDE.md suggestions — proposed changes to this file, recorded as recommendations only. Never edit CLAUDE.md as part of the recap.
- Suggestions — feature direction, and repo hygiene (code included).

## Behaviour
- This repo is heavily WIP. it's small enough that pervasive changes are doable since consumers are few.
- This repo keeps being worked on, by multiple agents at a time, so if something changes that you did not do, it's fine.
- Use worktrees when working on a large task, but if the task is small, avoid doing something heavy like worktrees and accept that sometimes the build breaks because multiple agents are working.

## Windows builds (remote)

`win32` builds run on a networked Windows box over SSH.

- **Host.** SSH alias `win-pilot` (`~/.ssh/config`: `gabbo-windows-desktop`, `100.120.188.120`, user `gabbo`, key `id_ed25519_winpilot`). 
- **Checkout.** `D:\repo\OrgWall` (repo root; no nested `melody/`), shares `origin`.
- **Build.** Bare SSH `cmd` lacks the MSVC/clang env; prefix with
  `C:\Users\Gabbo\dev.cmd` (loads vcvars64 + LLVM, execs the rest):

      ssh win-pilot "cd /d D:\repo\OrgWall && git pull --ff-only && C:\Users\Gabbo\dev.cmd nob build <target>"

- **Toolchain.** win32 CC is clang/MSVC; `nob` self-rebuilds via bare `clang` (dev.cmd provides it)
- **Workflow.** Commit+push here, `git pull`+build there; `origin` is source of truth.
