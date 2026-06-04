# jit

Backend-agnostic JIT interface. Defines `Mel_Jit_Backend` (a vtable: add/replace/remove/lookup/define_symbol/destroy) and the `Mel_Jit` façade that dispatches to whatever backend you plug in. Pure C, no LLVM.

The unit crossing the interface is `Mel_Jit_Module*` — an opaque LLVM IR module (the lingua franca), produced by a frontend (`clang`, or your own language via `llvm`) and consumed by a backend (`llvm`'s ORC, or any other IR-consuming JIT).

## Dependencies
`core`, `allocator`, `log`. No enums (MEL-CODE-001): backend selection is "hand me the vtable."

## Design
`design/jit-compiler.md` (master), `design/jit-interfaces.md` (signatures).
