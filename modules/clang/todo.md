# clang — review findings (2026-06-04)

## Resolved by the all-dynamic clang::Interpreter REPL (clang-frontend 4/4)
- **Result truncation (blocker)** — fixed: `clang::Value` carries the real type; `(int) 7`, `(double) 1.5`, etc. No more `long long` cast.
- **O(n^2) recompilation (major)** — fixed: `clang::Interpreter` is incremental; only the new input compiles.
- **Non-persistent globals (major)** — fixed: globals persist across lines (`int c=0; c+=41; c+1` → 42).
- **Keyword-whitelist classifier (major)** — fixed: the Interpreter classifies decl vs expression natively.
- **`-disable-free` AST leak (major)** — gone from the REPL path (no per-line fresh CompilerInstance; the Interpreter reuses one).

## Remaining
- **C++ mode, not pure C.** `clang::Interpreter` value-printing is C++-only, so the REPL evaluates the user's C as C++. Documented in the readme. A pure-C REPL with value capture would need custom work; `mel_clang_compile` is true C.
- **`mel_clang_compile` cleanups** (the our-jit path, unchanged this pass): `clang_resource_dir()` (dyld walk) + `getProcessTriple()` recomputed per call → cache; `-disable-free` still set there; `opt_level` honored but the IR→text→IR roundtrip can now be dropped (one shared LLVM) to hand the module to ORC directly.
- **`mel_clang_repl_lang` resource-dir** is computed once via the dyld scan; if libclang-cpp can't be located it falls back to clang's default — acceptable, but fail-loud would be stricter.
