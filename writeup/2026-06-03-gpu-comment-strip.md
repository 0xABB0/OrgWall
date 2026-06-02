# 2026-06-03 — gpu / hello-gpu comment strip

## Work done

Removed every hand-authored code comment from `modules/gpu/` and `apps/hello-gpu/`,
honoring the global "Never write comments" directive for these trees. Code, whitespace
structure, string/char literals, preprocessor directives, and line-continuations are
preserved byte-for-byte; only `//` and `/* … */` comments were excised.

- **123 files changed** (56 `.c`, 49 `.h`, 6 `.comp`, 5 `.frag`, 7 `.vert`); `+319 / -2121`.
- The one ObjC file (`src/vulkan/macos/surface.m`) had no comments → unchanged.
- Comment lines removed: **1767 pure-comment lines** plus **319 trailing-comment**
  code lines re-emitted comment-free (net −1802 lines after blank-line tidy).

### Method — tokenizer, not regex

A C/GLSL lexer state machine (`/tmp/stripcom/strip.py`, a host throwaway, not committed)
with states normal / line-comment / block-comment / string / char / (GLSL has no char),
handling `\` line-continuation and escaped quotes. Validated against an adversarial
fixture: `"http://…"`, `"/* not a comment */ and // neither"`, `'/'`, `'\''`, `'\\'`,
backslash-continued string literals, multi-line block comments, and `"//x"` inside a
macro — all preserved correctly.

Residue cleanup is line-continuation-aware: trailing whitespace exposed by a removed
trailing comment is stripped only on lines whose end-of-line lexer state is normal code
(never inside an open string or before a `\` continuation — those alignment-backslash
macro lines in `handle.h` are untouched). Full-line comments that emptied their line are
dropped; pre-existing blank lines are preserved; no doubled blank lines remain.

### `_spv.h` generated headers

Per the carve-out, only the **single leading banner** (the contiguous `//` block between
`#pragma once` and the first `#include`/`static const`) was removed from the 19 `*_spv.h`
files — 61 banner lines total. The `static const uint32_t … = { … };` numeric payload is
byte-identical (verified: the `_spv.h` diff is pure deletion of leading `//` lines, zero
payload bytes touched).

## Verification — the diff is comments-only (mechanical)

For all 123 changed files: tokenize(original-from-`HEAD`) == tokenize(current), where
`tokenize` = strip comments through the same lexer, then drop all whitespace. **123 OK,
0 mismatch.** Independent cross-checks: zero residual comments remain in any hand-authored
C/H/M/GLSL file; zero removed lines are commented-out code (every removed `//`/`/*` line is
descriptive prose — the semicolons in them are sentence punctuation, not C statements);
zero suspicious code-line deletions in the diff.

### Build + test (macOS, `--gpu=vulkan`, MoltenVK)

- `gpu-foundation`: **8/8** pass.
- `gpu-vulkan`: **37/37** pass, validation-clean (the one `[ERROR]` line is the intended
  negative test `vk_tracker.cross_thread_misuse_reports_without_aborting`).
- `gpu-stress` **16/16**, `gpu-concurrency` **7/7**, `gpu-visual` **11/11**,
  `gpu-bench` **12/12** — all pass; grep across each: **0 VUID, 0 leak mentions,
  0 validation errors**.
- `hello-gpu` builds (65/65, packaged). Headless 6s runs of `texquad`, `plasma`, `msaa`:
  device + 3-image swapchain created, rendered to timeout, **0 ERROR / 0 WARN / 0 VUID**
  in 246 log lines each.

## Kludges / caveats (MEL-ENGINE-VIII)

- **D3D12 compile-gap.** `src/d3d12/*.c` (16 files) and `test/test_d3d12.c` are
  `--gpu=d3d12`-gated and cannot build on macOS (no D3D12 SDK). They were stripped and
  **token-verified** (all OK), the same proof the compiled Vulkan files passed — but they
  are **not compile-verified here.** They must be built on `win-pilot` to close the gap.
- **`clang-format` not run.** It is available at `/opt/homebrew/opt/llvm/bin/clang-format`
  (v22). I deliberately did **not** run a blanket `clang-format -i`: ~29 in-scope files
  were already committed not-clang-clean, and reformatting them would inject ~1164 lines of
  reformatting churn unrelated to comment removal, violating "change nothing else." The
  stripper's own line-continuation-aware residue cleanup left **zero** trailing-whitespace
  or leading-space residue in the added lines, so no formatter pass was needed to keep the
  result clean. (Surveyed: zero mid-line block comments with code after them, so the
  inline-block leading-space case never arose in real files.)
- **`_spv.h` mid-payload comments preserved (load-bearing — flagged).** Two test headers
  carry descriptive comments *between* the byte arrays:
  - `modules/gpu/test/bindless_spv.h` (lines ~211, 283, 644–683): array banners plus the
    **embedded GLSL regeneration source** for `imgwrite.comp` / `fillargs.comp`.
  - `modules/gpu/test/visual_spv.h` (lines ~96, 184, 252–258, 410–539, 652–694): per-array
    banners and regeneration notes.
  The carve-out sanctions removing only the **single leading banner**; these mid-file
  comments sit inside the payload region (between `static const` arrays) and document how to
  regenerate the artifact, so I left them untouched and flag them for your ruling. If you
  want them gone too, that is a one-line widening of the `_spv.h` policy — say the word.

## CLAUDE.md suggestions

None. (Recorded as recommendation per process: a one-line note in `modules/gpu/readme.md`
that `*_spv.h` are generated and exempt from the no-comments rule would prevent a future
agent re-stripping their regeneration banners — optional.)

## Suggestions

- The `*_spv.h` regeneration sources currently live as comments in the generated headers
  and/or in `writeup/`. A `shaders/<name>.{comp,vert,frag}` + a codegen pass that emits the
  `_spv.h` (rather than hand-pasted `glslc -mfmt=c` output) would let the no-comments rule
  apply uniformly and kill the embedded-source comments outright (MEL-ENGINE-IX).
