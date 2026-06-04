#pragma once

#include <core/types.h>
#include <string/str8.fwd.h>
#include <allocator/allocator.fwd.h>
#include <jit/jit.h>
#include <repl/repl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    u32 opt_level; // host triple + c23 by default
} Mel_Clang_Config;

// C source -> LLVM IR module (via the `llvm` module). NULL + diagnostics in `out` on error.
Mel_Jit_Module* mel_clang_compile(const Mel_Alloc* a, str8 name, str8 source,
                                  const Mel_Clang_Config* cfg, Mel_Jit_Result* out);

// C as a REPL language: a Mel_Repl_Lang backed by clang::Interpreter (incremental compile +
// execute, with persistent declarations and typed result values). The Interpreter owns its own
// ORC JIT, so this path does not use the pluggable `jit` backend (which still serves
// `mel_clang_compile` and other frontends/backends).
Mel_Repl_Lang mel_clang_repl_lang(const Mel_Alloc* a);

#ifdef __cplusplus
}
#endif
