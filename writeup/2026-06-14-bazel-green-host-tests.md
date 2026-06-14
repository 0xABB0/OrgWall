# 2026-06-14 — Bazel: green `bazel test //...` on the host

## Work done — what changed, and why

Closed the gap the prior writeups nominated as the first follow-up: "the only thing
between 318 build and a green `bazel test //...` on the host." Three independent
runtime failures stood between the build and a green test run; all three are now fixed.
Host result: **104 tests pass, 2 skipped, 0 fail** (`--config=macos`). The lone
non-building target remains `apps/music-companion` — pre-existing source/API drift
(`mel_audiocapture_default_device` undeclared; `u32` passed where `Mel_AudioIn` is
expected), which nob fails identically. Not migration debt.

**① slang runtime dylib (13 tests → green).** The diagnosis in the prior writeup
("slang runtime plugin dylibs not staged") was imprecise. The real fault was a
*basename mismatch*: `slang_runtime` linked the `lib/libslang.dylib` symlink, but slang's
install id is `@rpath/libslang-compiler.0.2026.10.2.dylib`, so the binary records that
versioned name — while Bazel's solib symlink was named `libslang.dylib`. dyld failed at
load (`Library not loaded`), before `main`. Fix: link the real versioned compiler dylib
so the solib basename matches the install id —
`srcs = glob(["lib/libslang-compiler.0.*.dylib"])` in `bazel/third_party/slang.BUILD`
(glob excludes the unversioned symlink and tolerates patch bumps). No plugin co-location
was needed: the affected tests target SPIR-V/MSL/WGSL, all native slang emitters that
never dlopen the glslang/glsl-module/llvm bundles.

**② GPU test fork-safety (3 tests → green).** `gpu-metal`/`gpu-resources`/`gpu-scene`
crashed: `+[NSCheapMutableString initialize] ... when fork() was called`. The test runner
(`tools/test/src/runner.c`) forks per case for crash isolation; Metal spins up Obj-C
threads at framework load, so the multi-threaded parent forking → the child inherits a
locked Obj-C runtime → Apple crashes by design. This is documented in
`docs/verification.md` and `modules/gpu/readme.md`: GPU tests run under nob with
`MEL_TEST_NOFORK=1` (`paint`'s test self-sets it via constructor). Faithful port:
`env = {"MEL_TEST_NOFORK": "1"}` on every `gpu-*` cc_test (applied to all, not just the
three that crashed — the crash is a fork/thread race, so per-target determinism matters).

**③ GPU golden references unstaged (gpu-scene → green).** `img_golden.c` `fopen`s a
hardcoded workspace-relative path `modules/gpu/test/golden/<name>.ppm`. Under nob, cwd is
the repo root; under Bazel the sandbox cwd is the runfiles root, so the references must be
declared inputs. Fix: `data = glob(["test/golden/**/*.ppm"])` on the three tests that
compile `img_golden.c` (`gpu-scene`, `gpu-visual`, `gpu-webgpu`). `gpu-visual` passed
already only because it skips golden diffs on metal — but under `--gpu=vulkan` it diffs
too, so staging on all three is the complete fix, not just gpu-scene's.

Files touched: `bazel/third_party/slang.BUILD`, `modules/gpu/BUILD.bazel`.

## Kludges — every shortcut and the debt it leaves (MEL-ENGINE-VIII)

- **slang MH_BUNDLE plugins still unstaged.** `libslang-glslang`, `libslang-glsl-module`,
  `libslang-llvm` are `MH_BUNDLE` (filetype 8), dlopen'd by slang from the compiler dylib's
  own directory — they cannot go in `srcs` (`ld: unsupported mach-o filetype`), and Bazel's
  solib/rpath machinery offers no API to co-locate a runtime-only file in a linked library's
  solib subdir without linking it. No current macOS target needs them (SPIR-V/MSL/WGSL are
  native emitters). The honest position: any future use of GLSL output, host execution, or
  DXIL-off-win32 through slang will fail loudly at runtime — that is the trigger to build a
  custom co-location rule (symlink all dylibs+bundles into one dir owned by a single target,
  emit CcInfo with Bazel's per-consumer rpath). Deferred because YAGNI today, not because
  it's hard — flagging so it is not mistaken for "done."
- **`MEL_TEST_NOFORK` disables crash isolation for GPU tests.** Each `cc_test` is already its
  own process, so Bazel still isolates per-target; the loss is only intra-target (a crash in
  one case aborts that target's remaining cases). The fork model is fundamentally
  incompatible with Metal, so this is irreducible, not a shortcut — but it does mean the
  alarm-based per-case timeout in `run_isolated` no longer applies; a hung GPU case waits for
  Bazel's test timeout.

## Not my debt, but flagged

- `apps/music-companion` does not compile (pre-existing source/API drift; the prior writeup
  flagged it). Excluded from the green set; the app owner must update `companion.c`.

## CLAUDE.md suggestions (recommendations only — not applied)

- When the Bazel surface is documented, note that GPU tests carry
  `env = {"MEL_TEST_NOFORK": "1"}` and golden tests carry `data = test/golden/**` — so
  future GPU test authors copy the pattern rather than rediscover the fork crash.

## Suggestions

- The slang co-location rule (above) is the one piece of real machinery the third-party
  layer still owes; build it the first time a target needs a slang bundle, not before.
- Next sequenced frontiers (unchanged from prior writeups, all larger / externally gated):
  cross-platform verification (linux/win32/android/wasm/ios), packaging (`.app`/`.apk`),
  `run`/`debug` verbs, foreign third-party real builds, Android Java/manifest — then delete
  nob, `modules/build/`, and every `build.c`.
