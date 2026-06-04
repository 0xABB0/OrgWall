#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque LLVM IR module — the artifact crossing the frontend->backend boundary.
// Forward-declared here; defined by the `llvm` module.
typedef struct Mel_Jit_Module Mel_Jit_Module;

typedef struct Mel_Jit Mel_Jit;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Jit_Unit;

typedef struct
{
    bool             ok;
    char*            diagnostics; // owned by `alloc`; free via mel_jit_result_free
    const Mel_Alloc* alloc;       // allocator that owns diagnostics (set by the producer), or NULL
} Mel_Jit_Result;

// A pluggable execution backend (ORC, MCJIT, a custom IR-JIT, ...). `self` is the
// backend instance; the Mel_Jit facade dispatches every call through these pointers.
typedef struct
{
    void*          self;
    Mel_Jit_Unit   (*add)(void* self, Mel_Jit_Module* module, Mel_Jit_Result* out);
    Mel_Jit_Result (*replace)(void* self, Mel_Jit_Unit unit, Mel_Jit_Module* module);
    void           (*remove)(void* self, Mel_Jit_Unit unit);
    void*          (*lookup)(void* self, const char* symbol);
    void           (*define_symbol)(void* self, const char* symbol, void* address);
    void           (*destroy)(void* self);
} Mel_Jit_Backend;

Mel_Jit* mel_jit_create(const Mel_Alloc* allocator, Mel_Jit_Backend backend);
void     mel_jit_destroy(Mel_Jit* jit);

Mel_Jit_Unit   mel_jit_add(Mel_Jit* jit, Mel_Jit_Module* module, Mel_Jit_Result* out);
Mel_Jit_Result mel_jit_replace(Mel_Jit* jit, Mel_Jit_Unit unit, Mel_Jit_Module* module);
void           mel_jit_remove(Mel_Jit* jit, Mel_Jit_Unit unit);
void*          mel_jit_lookup(Mel_Jit* jit, const char* symbol);
void           mel_jit_define_symbol(Mel_Jit* jit, const char* symbol, void* address);

// Frees a result's diagnostics via the allocator the result carries (self-describing — no
// dependence on which Mel_Jit produced it). Safe on a zeroed/empty result.
void mel_jit_result_free(Mel_Jit_Result* result);

#ifdef __cplusplus
}
#endif
