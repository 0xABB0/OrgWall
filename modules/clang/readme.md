# clang

C-over-C++ wrapper of Clang: the C *frontend*.
- `mel_clang_compile(a, name, src, cfg, out)` → `Mel_Jit_Module*` (C source → LLVM IR, consumed by the pluggable `jit` backend — for kernels/plugins and any IR-consuming backend).
- `mel_clang_repl_lang(a)` → a `Mel_Repl_Lang` backed by **`clang::Interpreter`** (incremental compile+execute). Typed result values, persistent declarations/globals across lines, and decl-vs-expression classification all come from the Interpreter.

## Architecture
All-dynamic: clang and our ORC share one `libLLVM` (see `design/jit-vendor-llvm.md`), so `clang::Interpreter` owns its own ORC JIT against the *same* LLVM/target-registry. The REPL path therefore does **not** route through the pluggable `jit` backend (that still serves `mel_clang_compile` + custom languages + other backends).

The Interpreter runs in **C++ mode** (`IncrementalCompilerBuilder::CreateCpp`) and evaluates the C the user types as C++ (a near-superset) — value-printing in `clang::Interpreter` is C++-only, so a pure-C Interpreter mode is not available. `mel_clang_compile` is true C (`-x c -std=c23`).

`mel_clang_compile` drives a `CompilerInstance` (host triple, c23), emits IR via `CreateLLVMCodeGen`, and currently re-parses the IR text through `mel_llvm_parse_ir`. With one shared LLVM this roundtrip is no longer required for ABI safety and is a candidate to drop (hand the module to ORC directly).

## Dependencies
`llvm-runtime` (shared libLLVM + libclang-cpp), `llvm`, `jit`, `repl`, `core`, `allocator`, `string`, `log`.

## Design
`design/jit-interfaces.md`, `design/jit-consumers.md`, `design/jit-vendor-llvm.md`.
