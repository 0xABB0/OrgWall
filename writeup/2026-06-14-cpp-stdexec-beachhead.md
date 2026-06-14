# C++/stdexec beachhead — mixed-language seam over Bazel

## Work done

Established the toolchain beachhead for the C → C++ turn, proving the path stdexec
will travel before any spine code moves. Scope was agreed up front: mixed-language
model (C stays C23, C++ grafted at seams), beachhead only this session, with
stdexec as the eventual replacement for `executor` (~55 consumers) and `coro`
(3 external consumers).

- **stdexec via BCR `bazel_dep`, not vendored.** `MODULE.bazel` gains
  `bazel_dep(name = "stdexec", version = "0.0.0-20260427-35670665")` — the newest
  Bazel Central Registry snapshot. No `http_archive`, no hand-authored BUILD, no
  checked-in headers. BCR injects the header-only `@stdexec//:stdexec` cc_library
  via its `add_build_file.patch`. This is the swap point: when C++26
  `std::execution` is widely implemented, drop the dep and retarget the seam's
  include; nothing else moves.
- **`.bazelrc` C++ standard `c++17 → c++20`** (`--cxxopt`). stdexec's floor —
  concepts and coroutines sit in its public surface. Global change; verified safe
  for the existing host C++ TUs (`modules/clang`, `modules/llvm` both rebuild green
  at c++20).
- **`apps/hello-async` — the proof.** A C23 `main.c` calls `mel_async_probe()`
  defined in `seam.cc` (C++20), which runs
  `stdexec::sync_wait(stdexec::just(40) | stdexec::then(+2))` and returns 42 across
  an `extern "C"` boundary. `bazel run` prints `… = 42`, exit 0. This proves, in
  one target: C++20 compiles on the host toolchain, stdexec headers resolve, the
  C↔C++ seam links, and a sender executes. `target_compatible_with` gates it to
  macOS — the only verified toolchain. Bazel applies `-std=c2x` to the `.c` and
  `-std=c++20` to the `.cc` automatically (`--conlyopt`/`--cxxopt`), so the
  mixed-language `cc_binary` needs no per-file flag plumbing.

**Result:** `bazel run --config=macos //apps/hello-async` → `= 42`, exit 0;
`//modules/clang` and `//modules/llvm` still green at c++20.

## Kludges & debt (MEL-ENGINE-VIII — confessing all)

- **`hello-async` is a toolchain probe, framework-free by design.** It depends on
  `@stdexec` alone — no `boot`/`core`/`vat` — so a failure is unambiguously about
  the C++/stdexec toolchain, not the framework. Consequently it does not go through
  the melody boot entry point and is not a "real" app. It is a `cc_binary` with an
  exit-code assertion, **not** a `cc_test`, so `bazel test //...` does not exercise
  it. Promote to `cc_test` (or graduate it into the real spine) once the direction
  is committed.
- **The c++20 bump is global but only host-verified.** `modules/clang`/`modules/llvm`
  confirmed; `modules/geolocation/win32/src/geolocation_win32_iids.cpp` is
  win32-only and was **not** built here. The flag also rides every cross toolchain
  (zig-clang, emscripten, NDK-clang) on the next build of any C++ TU on those
  platforms — unverified.
- **stdexec proven on macOS only.** The dep and the seam are unexercised under
  emscripten / zig-clang / NDK-clang; stdexec at c++20 may have gaps on those
  libc++/libstdc++ stacks. No cross-toolchain compile of a sender has been done.
- **stdexec is pinned to a pre-1.0 BCR dev-snapshot** (`0.0.0-20260427-…`), a
  moving target with no semver stability. Acceptable for a beachhead; revisit when
  pinning the spine.
- **The actual replacement has not begun.** `executor` and `coro` are untouched —
  this session is the beachhead only, per the agreed scope.

## Not my debt, but flagged

- **Architectural reversal, recorded for consistency.** `design/vat-integration.md:200`
  deferred the sender/receiver layer in favour of `future+fiber+continuation`,
  "revisit only with a concrete need." Adopting stdexec reverses that. The concrete
  need was not captured in the repo; it should be written down where that decision
  lives, or the record contradicts the direction.
- **Concurrent migration.** Another agent edited `MODULE.bazel` mid-session
  (reordered the `bazel_dep` block so `apple_support` precedes `rules_cc`). The
  tree may keep shifting; my edits were kept minimal and composable.

## CLAUDE.md suggestions (recommendations only)

- Once C++ is blessed, document the **mixed-language idiom**: C TUs stay C23, C++
  TUs compile at c++20, a single `cc_library`/`cc_binary` links both, and cross-language
  surfaces go through an `extern "C"` seam. Without this, future agents will reflexively
  transliterate the C tree.
- Record that **stdexec is the concurrency direction** (replacing `executor`/`coro`),
  with the concrete need, so `design/vat-integration.md`'s deferral is consciously
  superseded rather than silently contradicted.
- **MEL-CODE-001** (no enums / tagged unions) needs an explicit C++ carve-out or
  re-affirmation: stdexec's tag types / CPOs are fine, but C++ tempts `enum class`
  and `std::variant` everywhere. State what is sanctioned before the team drifts.

## Suggestions

- **Sequence the replacement; do not flag-day it.** Retire `coro` first — 3 external
  consumers, codegen-based, contained — as the end-to-end proof of the migration
  pattern. Then tackle `executor` behind a **stable C-ABI seam** so its ~55
  consumers migrate incrementally; a single cutover across 45 modules + 10 apps is
  the kind of open-heart surgery MEL-ENGINE-VIII punishes.
- **Define the allocator bridge before deep adoption.** stdexec allocates through
  std allocators / `pmr`; MEL-CODE-003 mandates explicit mel allocators. Build a
  `pmr` adaptor over Mel allocators early (exec ships `__memory_resource_adaptor.hpp`
  to lean on), or the allocator discipline erodes the moment senders start
  allocating.
- **Decide c++20 vs c++23 deliberately, now.** c++23 buys `std::expected`, better
  ranges, and is the closer stepping stone to c++26 `std::execution`; stdexec
  compiles at both. If Apple clang/libc++ c++23 gaps are tolerable, bumping now
  avoids a second standard migration later.
- **Second beachhead before the spine: prove stdexec under one cross toolchain**
  (emscripten or zig-clang). MEL-ENGINE-VI/VII forbid leaving the weak platforms a
  broken shadow; the spine must not be committed on a macOS-only proof.

---

## Continuation — c++23 raised, `execution` module stood up

**Decisions taken (Gabbo):** standard → **c++23**; the `executor` replacement is
deferred to its own design; the **C interface lands now** as a new `execution`
module. stdexec supersedes the homebrew async layer because agents route *around*
it — a failed abstraction (MEL-ENGINE-V) — and the library spares us building
anything but type-erased C shims. The `vat-integration.md` deferral of
sender/receiver was sloth, not strategy (MEL-ENGINE-I names it); it is superseded,
the need being concrete and urgent.

**c++23.** `.bazelrc` `c++20 → c++23`. Verified: stdexec, the LLVM-bound TUs
(`modules/clang`, `modules/llvm`), and `hello-async` all compile and run under
Apple clang/libc++ at c++23; `hello-async` still prints `= 42`.

**`modules/execution` — the type-erased C-ABI seam over stdexec.**
- Surface: `Mel_Execution_Sender` (opaque, one-shot),
  `mel_execution_sender_create(alloc, work, ctx)`, `mel_execution_sync_wait`,
  `mel_execution_sender_destroy`. C header is `extern "C"` (`.h`); impl is C++23
  (`.cc`).
- Type erasure via the non-deprecated
  `exec::any_sender<exec::any_receiver<completion_signatures<set_value_t(void*)>>>`
  (the upstream `any_sender_of` alias is deprecated). The C callback is lifted into
  `just() | then(noexcept λ)`, which keeps the erased completion signature to a
  single value channel.
- Allocator discipline (MEL-CODE-003): handle storage is
  `mel_aligned_alloc`/`mel_aligned_dealloc` through the caller's `Mel_Alloc`.
- Two C tests pass (`bazel test //modules/execution:execution-test`): a value
  round-trips through the seam (1 → 42), and create→destroy under a counting
  allocator leaves zero live allocations.

### Kludges & debt (MEL-ENGINE-VIII — confessing all)

- **`execution` is Bazel-only — no `build.c`.** nob cannot build C++23/stdexec.
  Verified harmless to nob: `discovery.c:107` returns success and skips any dir
  lacking `build.c`, so the module is invisible to nob (not an error). But it
  *will not build on the nob path at all*, widening the nob/Bazel divergence until
  nob is retired.
- **macOS-only.** `target_compatible_with` gates the library and test to
  `plat_macos`; stdexec is unexercised on zig-clang/emscripten/NDK. The c++23 flag
  also rides those toolchains on the next build of any C++ TU there — unverified.
- **Inner allocations escape the allocator.** Only the *handle* uses `Mel_Alloc`.
  stdexec's own SBO-overflow allocations (a sender outgrowing its inline buffer)
  go through global `operator new`. Our lambda fits the SBO, so the no-leak test
  sees an exact balance — but the general bridge (a `pmr` resource over
  `Mel_Alloc`; exec ships `__memory_resource_adaptor.hpp`) is deferred to the
  executor-replacement design. **This is the concrete form of the allocator
  question.**
- **One-shot, no scheduler, no composition.** `sync_wait` consumes the sender and
  runs the work inline on the caller. No `then`/`when_all` in the C surface, no
  scheduler — those are the executor-replacement design, not the seam seed.

### CLAUDE.md suggestion (recommendation only)

- Record the mixed-language idiom now in force: `.c` (C23) and `.cc` (C++23)
  coexist in one target; C++ headers are `.hh`; the C-ABI seam is `extern "C"` in a
  `.h`; and C++-only modules may be **Bazel-only with no `build.c`** (nob skips
  them). This stops the next agent from adding a `build.c` to `execution` (and
  breaking the nob path) or transliterating the C tree.
