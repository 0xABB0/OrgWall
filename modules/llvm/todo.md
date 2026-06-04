# llvm — review findings (2026-06-04)

## Fixed and verified (llvm-orc 6/6)
- **Module leak (blocker).** `mel_orc_add`/`mel_orc_replace` now take ownership of the `Mel_Jit_Module` and free the wrapper on every post-guard path (success and failure). Tests never free the module; no leak.
- **Process-symbol isolation (blocker).** `expose_process_symbols=false` now routes user code into a *bare* JITDylib (`ExecutionSession::createBareJITDylib`, no default link order), so a JIT'd `getpid()` fails to resolve ("Symbols not found: [ _getpid ]") and lookup returns NULL; `true` uses the main dylib and resolves. `setLinkProcessSymbolsByDefault(false)` was rejected — it also starves the platform bootstrap and breaks `create()`.
- **`replace` handle stability (major).** Generation is no longer bumped on a successful `replace`, so the caller's `Mel_Jit_Unit` stays valid; a second replace re-resolves correctly (tested).
- **Unbounded slot growth (major).** Removed slots are pushed to a free-list and reused by `add`; generation bumps only on slot reuse (ABA-safe).
- **`replace` failure no longer half-states silently**, NULL-tracker OOM now rolls back instead of storing a live unit with a null tracker, and `MangleAndInterner` is cached on the backend.

New tests: isolation on/off, replace-keeps-handle, remove-then-lookup-NULL, define_symbol-callable-from-JIT, plus execute + bad-IR.

## Resolved by the all-dynamic pivot (shared libLLVM.dylib)
- **In-process LLVM duplication + slow LTO relink** — gone: one shared `libLLVM.dylib` (Homebrew) linked by both `llvm` and `clang`; dynamic link is fast.
- **Hardcoded zstd path / over-linked disassembler / duplicate `-lc++`** — gone with the static archive set; `third-party/llvm/build.c` is now just `-lLLVM -lclang-cpp` + include/rpath.

## Remaining — interface (M-5)
- `lookup` returns only `void*`; the actionable ORC error ("Symbols not found: [...]") is logged then discarded. Add an out-diagnostic channel to `Mel_Jit_Backend.lookup` + `mel_jit_lookup`, surfaced to callers. (The REPL no longer needs this — it uses clang::Interpreter's own diagnostics — but the `mel_jit_lookup` path for kernels/plugins still does.)

## Cross-platform (see design/jit-vendor-llvm.md)
- Linux/Windows dynamic-LLVM wiring + `llvm-config`/`brew --prefix` path discovery to replace the pinned macOS prefix.

## Remaining — interface, pending the M-5 pass
- `lookup` returns only `void*`; the actionable ORC error ("Symbols not found: [...]") is logged then discarded. Plan: add an out-diagnostic channel to the `Mel_Jit_Backend.lookup` vtable + `mel_jit_lookup`, surfaced through the REPL.
