# repl

Backend-agnostic read-eval-print loop. Defines `Mel_Repl_Lang` (a vtable: evaluate a source line in a session → a printable `Mel_Repl_Result`) and the loop (dispatch, session, history). Pure C — no LLVM, no `jit`, no language baked in.

Plug in a language by supplying a `Mel_Repl_Lang`. C is the first one (`mel_clang_repl_lang` in the `clang` module); your own language is another.

## Dependencies
`core`, `allocator`, `string`, `log`.

## Design
`design/jit-interfaces.md` (signatures), `design/jit-consumers.md` (loop semantics).
