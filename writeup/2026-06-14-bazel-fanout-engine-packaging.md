# 2026-06-14 — Bazel migration: parallel-agent fan-out (engine fixes + packaging)

## Work done — what changed, and why

Continued the Bazel migration by fanning the parallelizable, file-disjoint tasks out to agent teams.
Two waves, each integrated and verified on the macOS host (no regression), three commits total
(`6cd2efbf` coro, `e1acb30a` engine, `89ea7ed2` packaging).

### Wave 1 — engine source (3 agents, worktree-isolated)

- **Linux `vat` waiter** — `modules/vat/linux/src/waiter_epoll.c`: epoll + an eventfd doorbell
  (coalesced via an atomic flag) implementing the full `Mel_Vat_Waiter_Vtbl`
  (arm/disarm/wait/ring/close), mirroring the darwin kqueue backend. Declared `mel_vat_waiter_epoll`;
  added a `plat_linux` arm to `modules/vat/BUILD.bazel`. Was: `undefined symbol mel_vat_waiter_io` on
  Linux — the single blocker for *all* Linux linking. Verified `//modules/vat --config=linux`.
- **Android `process` backend** — bionic lacks the whole `posix_spawn` family below API 28 (and
  `addchdir_np` below 34), so the cwd path can't use it. Android now uses `fork` + `dup2`/`chdir`/
  `setsid` + `execvpe` with a `CLOEXEC` report pipe carrying child-side errno back (so exec failures
  surface identically); macOS/Linux keep `posix_spawnp`. In-source `#if defined(__ANDROID__)`,
  matching the module's idiom; no BUILD change (posix backend already staged on android). Verified
  `//modules/process --config=android` (libprocess.so) + macos tests pass.
- **music-companion** — `companion.c` called a nonexistent `mel_audiocapture_default_device` and
  passed wrong types. Retargeted onto `audioin` (`mel_audioin_default`/`authorize`) + the
  struct-returning `mel_audiocapture_open`; added audioin/executor/future/thread deps to its
  BUILD.bazel. Verified `//apps/music-companion --config=macos`. All 85 app targets now build on macos.

### Wave 2 — bazel authoring (3 agents, NON-isolated, shared worktree)

- **wasm + win32 packaging** — `bazel/rules/mel_wasm.bzl` (emscripten `.html` via `--shell-file`,
  ported `web/shell.html`; a `bazel run` dev server with COOP/COEP/no-store from `serve.py`) and
  `bazel/rules/mel_win32_resources.bzl` (`llvm-rc` on a templated `.rc` + `/subsystem:windows`).
  Proven on `hello-world-gui`: `:hello-world-gui_web_html --config=wasm` produces a `.html`; the
  win32 path analyzes; macos unaffected. ROLLOUT to the remaining GUI apps is mechanical follow-up.
- **slang prebuilts** — `slang_linux` + `slang_win32` http_archives (sha-pinned) + per-platform build
  files; extended the `//third-party/slang` select. v2026.10.2 publishes **no android artifact**
  (correctly omitted). Verified `--config=linux` builds, macos no regression.
- **xwin pin** — pinned all three prebuilt sha256s; fixed a latent bug (windows artifact is
  `.tar.gz`, was declared `.zip` → would 404 on a Windows exec host).

## Kludges & debt (MEL-ENGINE-VIII)

- **wasm/win32 packaging wired on ONE app only** (`hello-world-gui`). The rules are authored and
  proven; the remaining ~13 GUI apps need the `mel_wasm_app`/`mel_win32_resources` wiring. Mechanical
  but not done.
- **No android slang prebuilt exists upstream** — gpu→slang on android (ballgame/hello-gpu apk) stays
  blocked until slang is built-from-source for the NDK or a community prebuilt appears. Not a
  packaging defect.
- **A5 partial** — xwin pinned; the macOS Vulkan pin (`modules/gpu` raw `/opt/homebrew`) and the
  musictheory/musicnotation `MEL_PRIVATE` include-leak are still open.

## Process note — orchestration lesson (important for future fan-outs)

`isolation: "worktree"` branches each agent's worktree from the repo's **`main`** (the nob-only
tree), NOT from the active `worktree-bazel-migration` branch. So isolated agents could not see any
`BUILD.bazel`/`MODULE.bazel`; their build-config edits landed in nob's `build.c`. Their *source*
changes were still correct (the `.c`/`.h` files are identical across both trees), so wave-1 was
recovered by lifting the source files into the bazel tree and authoring the `BUILD.bazel` gating here.
**Wave-2 used NON-isolated agents** (run in the bazel worktree directly) — they saw the bazel files,
shared the warm cache, and landed changes in place with no patch-integration. For bazel-specific work,
prefer non-isolated agents with strictly file-disjoint scopes (and instruct them to run no
git-mutating / `bazel clean` commands).

## Suggestions / remaining plan

Untouched tracks: A4 (iOS bundling), C (Linux sysroot — now unblocked on the engine side by the vat
waiter; still needs the vendored `-dev` sysroot), D3/D4 (Dawn/vulkan-loader), E1/E2 (win32 from
macОS/Windows hosts), packaging rollout, then F (per-platform verification) and G (delete nob). See
`~/.local/claude/work/plans/prancy-beaming-manatee.md`.
