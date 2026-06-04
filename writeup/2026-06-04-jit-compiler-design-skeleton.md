# JIT/REPL compiler integration — design, build-out, all-dynamic resolution

## Work done
Built a runtime C compilation + REPL capability, decomposed into four modules and wired to LLVM/Clang, taken from design through a working end-to-end REPL.

**Architecture (four modules, two pluggability axes).**
- `jit` — pure-C backend interface (`Mel_Jit_Backend` vtable + façade). Artifact = `Mel_Jit_Module*` (LLVM IR). No LLVM.
- `repl` — pure-C loop over a `Mel_Repl_Lang` vtable (source/sink/prompts, history, continuation). No LLVM.
- `llvm` — C++ shim: parse IR + an ORCv2 `Mel_Jit_Backend`.
- `clang` — C++ shim: `mel_clang_compile` (C→IR for the pluggable jit) + `mel_clang_repl_lang` (the C REPL via `clang::Interpreter`).
- `apps/repl` (`repl-cli`) — native C REPL demo.

**Toolchain — all-dynamic, first-class (the load-bearing decision).** After hitting two walls — (1) all-static crashes at startup on the LLVM-21 prebuilt (clang21/libc++21 typed-new TMO global-ctors run in our Apple-clang-17 binary before libc++abi init), and (2) static-LLVM + embedded-LLVM-in-`libclang-cpp` gives two `TargetRegistry`s so `clang::Interpreter` can't find a target — we pivoted to one **shared `libLLVM.dylib`** (Homebrew on macOS) linked by both `llvm` and `clang`. One LLVM image, one registry, exported C API, dynamic ctor ordering safe at load. `third-party/llvm/build.c` is now `-lLLVM -lclang-cpp` + include/rpath; the 7.8 GB static download path is gone.

**REPL via `clang::Interpreter`.** The C REPL uses clang's incremental `Interpreter` (its own ORC JIT on the shared libLLVM), which delivered typed result values, persistent globals, incremental compile (no O(n²)), and native decl/expr classification in one move. It does not route through the pluggable `jit` backend; `mel_clang_repl_lang(a)` dropped its `Mel_Jit` parameter.

**Review (3-agent team) + fixes.** A correctness redteam, a wasted-byte/cycle audit, and a DX/coverage pass reviewed the first implementation; findings recorded in `modules/{llvm,clang}/todo.md`. Fixed and verified:
- `llvm` (in `src/llvm.cpp`): module-leak on add/replace (now owns+frees), process-symbol isolation when `expose_process_symbols=false` (bare JITDylib — a JIT'd `getpid` is rejected), `replace` keeps the caller's handle (no gen bump), free-list slot reuse, NULL-tracker rollback, cached mangler. +6 contract tests.
- Result structs made self-describing (carry their allocator) — `mel_jit_result_free`/`mel_repl_result_free`.
- `clang` REPL: truncation, O(n²), globals, classifier all resolved by `clang::Interpreter`.

**Verified:** jit-facade 2/2, repl-loop 7/7, llvm-orc 6/6, clang-frontend 4/4; `repl-cli` evaluates `1+2*3`→`(int) 7`, defines `sq`, `sq(9)`→`(int) 81`, `int c=0; c+=41; c+1`→`(int) 42` (globals persist), `3.0/2.0`→`(double) 1.5` (typed). An unrelated module (`paint-pixmap` 3/3) confirms the build-framework edits didn't regress.

## Kludges (bar is zero; full confession)
- **macOS Homebrew LLVM path hardcoded** (`/opt/homebrew/opt/llvm`) and the build now **depends on a system `brew install llvm`** — not vendored/reproducible like `mel_prebuilt`, and no `llvm-config`/`brew --prefix` discovery yet. Linux/Windows are unwired (Windows `clang::Interpreter` unproven — the platform Gabbo flagged).
- **REPL runs C as C++.** `clang::Interpreter` value-printing is C++-only, so the Interpreter is created with `CreateCpp()`; the user's C is evaluated as C++ (near-superset). Pure-C value-returning REPL isn't available via `clang::Interpreter`.
- **`mel_clang_compile` still does the IR→text→IR roundtrip** (now unnecessary with one shared LLVM) and recomputes resource-dir/triple per call and sets `-disable-free`. Cleanups deferred.
- **M-5 not done:** `mel_jit_lookup` still returns only `void*`; the "symbol not found" diagnostic is logged then discarded (matters for the kernels/plugins jit path, not the REPL).
- **Result-struct field reorder (8-byte nit) not done.**
- **Orphaned 7.8 GB** static LLVM download under `third-party/llvm/build/` is now unused (build.c no longer references it); should be cleaned.
- A debug `logAllUnhandledErrors(... llvm::errs() ...)` remains in the clang Interpreter-create error path (writes to stderr rather than the mel log).
- The build framework was extended (by an earlier agent): `discovery.c` slugifies the `_loadc` loader name (a real, necessary fix — `modules/llvm` and `third-party/llvm` collided), `thirdparty.c` added `.tar.xz` + LTO-lib handling (the latter now moot under all-dynamic).

## CLAUDE.md suggestions (recommendations only)
- `<string/str8.h>` is not C++-includable (its `.inl` uses implicit `void*→u8*`); document `<string/str8.fwd.h>` as the C++-safe entry for shim authors.
- The build framework lacks a "run a command, capture flags" hook; this forces hardcoded paths (zstd before, now the LLVM prefix). A `mel_pkg_config`/`mel_shell_flags` primitive would let `llvm-config`/`brew --prefix` drive vendoring portably.

## Suggestions
- Make the LLVM prefix discoverable (`llvm-config --libdir/--includedir`, `brew --prefix llvm`) and wire Linux/Windows before promising the REPL cross-platform.
- Drop the `mel_clang_compile` IR-text roundtrip (one LLVM now) and cache resource-dir/triple.
- Decide whether the REPL's C-as-C++ behavior is acceptable or warrants a custom pure-C value-printing path.

## Next steps (explicit, ordered)
1. **Cross-platform LLVM (highest priority — currently macOS-only).**
   - Replace the hardcoded `/opt/homebrew/opt/llvm` with discovery: `llvm-config --libdir/--includedir/--version` or `brew --prefix llvm`. This needs a build-framework primitive to run a command and capture flags (absent today — see CLAUDE.md suggestion).
   - Linux: wire distro `libLLVM-NN.so` + `libclang-cpp.so` (same shared-libLLVM shape).
   - Windows: validate `clang::Interpreter` against the LLVM installer's `libclang-cpp`/`LLVM-C` (the unproven piece). If it can't be made to work, `mel_unavailable` the REPL on Windows but keep `mel_clang_compile` + the ORC path (which use the C API and should port).
2. **M-5 — surface lookup diagnostics.** Add an out-`Mel_Jit_Result*` to `Mel_Jit_Backend.lookup` + `mel_jit_lookup` so "Symbols not found: [...]" reaches callers (kernels/plugins). Ripples to `jit.h` vtable, façade, `llvm.cpp`, `clang.cpp` (mel_clang_compile path), and the mock tests.
3. **`mel_clang_compile` cleanups (one shared LLVM now).** Drop the IR→text→IR roundtrip — add `mel_llvm_wrap_module(LLVMModuleRef, ctx)` to `<llvm/llvm.h>` and hand the `llvm::Module` to ORC directly; cache `clang_resource_dir()`/`getProcessTriple()`; drop `-disable-free`.
4. **REPL language decision.** Accept C-as-C++ (document in user-facing docs) or invest in a pure-C value-printing path.
5. **Hygiene.** Reorder result structs (pointers-first, 8 bytes); replace the debug `logAllUnhandledErrors(... errs())` in clang Interpreter-create with `mel_log_error`; clean the orphaned ~7.8 GB static LLVM download under `third-party/llvm/build/` (build.c no longer uses it).
6. **The other three consumer use cases** (REPL is done; these are the rest of the original scope):
   - **Hot-reload** — `mel_jit_replace` exists and is tested; add a file-watch + state-migration layer (`design/jit-consumers.md`).
   - **Kernels** — a compile-function-pointer cache keyed by source hash + opt_level over `mel_clang_compile` + the ORC backend.
   - **Plugins** — a capability allow-list over `mel_jit_define_symbol`, leveraging the now-working `expose_process_symbols=false` isolation (bare JITDylib).
7. **Spec tidy (MEL-SPEC-002).** Fold `design/jit-*.md` into the four modules' own docs as the interfaces stabilize; delete the design/ copies.
