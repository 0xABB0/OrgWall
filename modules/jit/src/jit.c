#include <jit/jit.h>

#include <allocator/allocator.h>
#include <log/log.h>

struct Mel_Jit
{
    const Mel_Alloc* allocator;
    Mel_Jit_Backend  backend;
};

Mel_Jit* mel_jit_create(const Mel_Alloc* allocator, Mel_Jit_Backend backend)
{
    if (!allocator)
    {
        mel_log_error("jit", "mel_jit_create requires a non-null allocator");
        return NULL;
    }
    if (!backend.add || !backend.lookup)
    {
        mel_log_error("jit", "mel_jit_create requires a backend with at least add + lookup");
        return NULL;
    }

    Mel_Jit* jit = mel_alloc_type(allocator, Mel_Jit);
    if (!jit)
        return NULL;

    jit->allocator = allocator;
    jit->backend   = backend;
    return jit;
}

void mel_jit_destroy(Mel_Jit* jit)
{
    if (!jit)
        return;
    if (jit->backend.destroy)
        jit->backend.destroy(jit->backend.self);
    mel_dealloc(jit->allocator, jit);
}

Mel_Jit_Unit mel_jit_add(Mel_Jit* jit, Mel_Jit_Module* module, Mel_Jit_Result* out)
{
    return jit->backend.add(jit->backend.self, module, out);
}

Mel_Jit_Result mel_jit_replace(Mel_Jit* jit, Mel_Jit_Unit unit, Mel_Jit_Module* module)
{
    if (!jit->backend.replace)
    {
        mel_log_error("jit", "backend does not implement replace");
        return (Mel_Jit_Result){ .ok = false, .diagnostics = NULL };
    }
    return jit->backend.replace(jit->backend.self, unit, module);
}

void mel_jit_remove(Mel_Jit* jit, Mel_Jit_Unit unit)
{
    if (jit->backend.remove)
        jit->backend.remove(jit->backend.self, unit);
}

void* mel_jit_lookup(Mel_Jit* jit, const char* symbol)
{
    return jit->backend.lookup(jit->backend.self, symbol);
}

void mel_jit_define_symbol(Mel_Jit* jit, const char* symbol, void* address)
{
    if (jit->backend.define_symbol)
        jit->backend.define_symbol(jit->backend.self, symbol, address);
}

void mel_jit_result_free(Mel_Jit_Result* result)
{
    if (!result)
        return;
    if (result->alloc && result->diagnostics)
        mel_dealloc(result->alloc, result->diagnostics);
    result->diagnostics = NULL;
    result->alloc       = NULL;
}
