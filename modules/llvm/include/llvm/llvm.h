#pragma once

#include <core/types.h>
#include <string/str8.fwd.h>
#include <allocator/allocator.fwd.h>
#include <jit/jit.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    u32  opt_level;             // 0..3 -> LLVM -O
    bool expose_process_symbols; // host-process symbols reachable from JIT'd code
} Mel_Llvm_Orc_Config;

// IR production. parse_ir is today's entry for a custom language (emit IR text, parse it);
// an IR-builder surface follows. Returns NULL + diagnostics in `out` on error.
Mel_Jit_Module* mel_llvm_parse_ir(const Mel_Alloc* a, str8 name, str8 ir_text, Mel_Jit_Result* out);
void            mel_llvm_module_free(const Mel_Alloc* a, Mel_Jit_Module* module);

// ORC implementation of Mel_Jit_Backend. Plug into mel_jit_create.
Mel_Jit_Backend mel_llvm_orc_backend(const Mel_Alloc* a, const Mel_Llvm_Orc_Config* cfg);

#ifdef __cplusplus
}
#endif
