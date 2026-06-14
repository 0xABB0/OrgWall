# 2026-06-14 — Bazel migration: retire `coro`, retarget dependents to C++ coroutines

## Work done — what changed, and why

Continued the Bazel migration toward full nob deletion. This session closed the **coro** track:
the codegen-based `modules/coro` (a libclang host tool generating C state machines) is deprecated —
the framework moved to C++, where coroutines are first-class — so it was retired and its three
dependents retargeted onto native C++ coroutines.

**Third-party generators library — `third-party/cppcoro` (vendored, header-only).** `std::generator`
(C++23) is unavailable on the toolchains (Apple clang 17 ships no `<generator>`; the NDK/emscripten/
clang-cl matrix is no better), so the standard path is not viable. Vendored the 4-header
`generator`/`recursive_generator` closure from `andreasbuhr/cppcoro` (MIT, commit `8642e98`):
`config.hpp`, `coroutine.hpp`, `generator.hpp`, `recursive_generator.hpp`. Probed green on the host
at C++20 and C++23.

**These modules are now bazel-only C++** (Gabbo's explicit OK against the "don't break nob" directive,
Rule #1): nob compiles C++ only at `-std=c++17` and the modern-C++ modules (`execution`, `hello-async`)
already have no `build.c`. So each retargeted module's `build.c` was deleted; nob no longer builds them.

- **`musicnotation`** — `chord_id.c`→`chord_id.cc`; the internal chord-identify generator is now a
  `cppcoro::generator<Mel_Chord_Match>`; the public C API `mel_chord_identify` is unchanged.
  `notation-test` green.
- **`musictheory`** — `scale.c`→`scale.cc`; all 7 generators are internal C++ (anonymous namespace);
  the public set-op functions drive 4 of them. Added 3 `extern "C"` bounded accessors
  (`mel_scale_pitches`, `mel_scale_stream`, `mel_pattern_pitches`) so the C test still exercises the
  pitch/stream/pattern sequences (the stream is infinite — the accessor takes a bounded count, proving
  laziness). `musictheory-test` green.
- **`compress-lab`** — `job.coro.h`→`job.cc`; `lab_pump`/`lab_race_run` are now `recursive_generator`s
  (race delegates to pump). Because `ui.c` keeps the coroutine as GUI state and steps it once per UI
  tick within a time budget, it needs a **persistent resumable handle**, not a run-to-completion call:
  added an opaque `extern "C"` handle (`lab_pump_begin/step/end`, `lab_race_*`) in `job.cc`; `ui.c` and
  `smoke.c` stay C. Builds green (binary + macOS `.app`).

**`modules/coro` deleted (A1).** No target depends on it anymore. The `mel_codegen` rule is retained
(`input`/`gamepad`/`display` still generate tables with it), and the `@llvm` extension with it (the
rule loads `@llvm//:sdk.bzl`). Result: **274 module targets build, 97 tests pass / 2 skip (the shelved
clang/llvm) on `--config=macos`** — no regression.

### Header-hardening tail (the real, recurring cost of going C++)

Making a C module callable from a C++ TU needs `extern "C"` on its cross-TU declarations, plus the odd
C-only construct fixed. Hardened this session (each harmless in C):
- `string/str8.inl`, `collection/array.h` — explicit casts on implicit `void*`→`T*` (illegal in C++).
- `string/str8.h`, `musictheory/{scale,pitch,interval,pattern,chord}.h`, `compress-lab/lab.h` —
  `extern "C"` guards around the non-inline declarations.

## Kludges & debt (MEL-ENGINE-VIII — confessing all)

- **cppcoro frames + the compress-lab handles use global `new`/`delete`** (`job.cc`), bypassing the
  engine allocator (MEL-CODE-003). cppcoro's generator promise allocates its frame via global
  `operator new` with no custom-allocator hook, so even a handle allocated through `Mel_Alloc` could not
  make the coroutine frame itself allocator-aware. `lab_*_begin` deliberately takes **no** allocator
  (rather than take one and silently ignore it — MEL-CODE-007). Debt: a coroutine task/generator type
  with a `Mel_Alloc`-aware promise if allocator accounting over coroutines is ever wanted.
- **`math/real.h` is a hard C++ wall, documented not fixed.** It is built on `MEL_OVERLOADABLE`
  (`mel_real`, `mel_real_mul`, `mel_real_div`, `mel_real_ratio` are overloaded). `extern "C"` forbids
  overloading, and as plain C++ the symbols mangle differently than the C-compiled `libmath` — so any
  C++ TU that transitively reaches the math stack (`frequency/cent` → `math/real`) **cannot link**.
  Consequence: comprehensive tests over the music/math stack must stay `.c`. This is why the retarget
  pattern is "generators internal C++ + `extern "C"` accessors + tests stay C," not "convert the test."
- **3 modules are now bazel-only.** `musicnotation`/`musictheory`/`compress-lab` have no `build.c`; nob
  cannot build them (sanctioned by Gabbo). Their nob-built dependents (e.g. `midi`→`musictheory`) are
  correspondingly broken under nob until nob is deleted. Not a Bazel regression.
- **New public API surface.** `mel_scale_pitches`/`mel_scale_stream`/`mel_pattern_pitches` were added
  primarily to keep the test in C; they are reasonable C accessors for the lazy sequences but are, in
  part, test-driven API.
- **`extern "C"` guards added speculatively** to `pitch/interval/chord.h` (the test only needed some).
  Harmless and forward-looking, but they are ahead of a present consumer.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document the C↔C++ boundary policy now that C++ is in the tree: C public headers consumed by C++
  must carry `#ifdef __cplusplus extern "C"` guards; `MEL_OVERLOADABLE` APIs (string, math) are **not**
  C++-linkable and bound which TUs can be C++. State that tests over those stacks stay C.
- Note that C++ modules are Bazel-only (nob is C++17, no coroutine support) until nob is deleted.

## Suggestions

- When the next module goes C++, expect the same header-hardening tail. A one-pass sweep adding
  `extern "C"` guards to the core C headers (core/allocator/collection/string/time/…) would front-load
  it — but leave `MEL_OVERLOADABLE` headers (string, math) alone; they need a deliberate C++ redesign
  (drop `overloadable` for real C++ overloads compiled once as C++), which is its own project.
- The remaining plan tracks are untouched this session: packaging (wasm/win32/iOS), the engine-source
  blockers (Linux `vat` waiter, Android `process`), the Linux sysroot, foreign-tp prebuilts, win32
  from all hosts, then delete nob. See `~/.local/claude/work/plans/prancy-beaming-manatee.md`.
