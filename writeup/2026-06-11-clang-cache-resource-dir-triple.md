# clang: cache resource_dir and process triple; drop -disable-free

## Work done

`modules/clang/src/clang.cpp` — three changes:

1. `clang_resource_dir()` converted from a function that runs a full dyld image scan on every call to one that runs the scan once via a `static const std::string` initialized by an immediately-invoked lambda. Return type changed from `std::string` (copy) to `const std::string&`.

2. `clang_process_triple()` added — `static const std::string` wrapping `llvm::sys::getProcessTriple()`. The triple does not change for the lifetime of the process. Previously `clang_emit_ir` called `getProcessTriple()` on every invocation, allocated a temporary, then discarded it.

3. `-disable-free` removed from the compiler args vector in `clang_emit_ir`. The flag suppresses deallocation of Clang/LLVM internals on exit, which is only meaningful in a short-lived compiler driver process where skipping frees saves shutdown time. In a long-lived library context where `CompilerInstance` is constructed and destroyed per call, the flag has no positive effect and marginally misleads readers about lifetime intent.

Call sites in `mel_clang_repl_lang` also updated to bind the resource-dir result to `const std::string&` rather than copy it.

Build verified: `./nob build clang macos` — clean, no warnings.

## Kludges

None.

## CLAUDE.md suggestions

None.

## Suggestions

- The dyld scan in `clang_resource_dir` is macOS-only (`mach-o/dyld.h`). If the module is ever built on Linux or Windows the function will need a platform axis. The static-local pattern is the right shape for that too; each axis provides its own discovery body, the caller is unchanged.
- `clang_emit_ir` creates a fresh `LLVMContext` and `CompilerInstance` on every call. For high-frequency JIT use (e.g., REPL-driven hot reload), a persistent `CompilerInstance` with remapped sources would be a larger win than these micro-optimizations. Worth tracking in `todo.md`.
