# llvm

C-over-C++ wrapper of the LLVM library. Two surfaces:
- **IR**: build/parse LLVM IR → `Mel_Jit_Module` (`mel_llvm_parse_ir` today; an IR-builder follows). This is the entry for *your own language*: emit IR, run it — no C frontend needed.
- **ORC backend**: `mel_llvm_orc_backend()` returns a `Mel_Jit_Backend` (ORCv2 LLJIT) to plug into `mel_jit_create`.

Defines `struct Mel_Jit_Module` (wraps an `LLVMOrcThreadSafeModule`); the `jit` interface only forward-declares it.

## Status
Skeleton. C surface fixed; backend loud-stubbed, no toolchain linked yet. Wiring adds `mel_depends(lib, "llvm-runtime")` (the vendored prebuilt).

## Dependencies
`jit`, `core`, `allocator`, `string`, `log`; + `llvm-runtime` when wired.

## Design
`design/jit-interfaces.md`, `design/jit-vendor-llvm.md`.
