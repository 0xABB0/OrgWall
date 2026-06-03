#include "slang/compile.h"

#include <slang.h>
#include <slang-com-ptr.h>

#include <cstdlib>
#include <cstring>

using Slang::ComPtr;
using namespace slang;

static char* mel_slang__dup(const char* s)
{
    if (!s)
        return nullptr;
    size_t n = strlen(s) + 1;
    char*  d = (char*)malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

static const char* mel_slang__diag(IBlob* diag, const char* fallback)
{
    if (diag && diag->getBufferPointer() && diag->getBufferSize())
        return (const char*)diag->getBufferPointer();
    return fallback;
}

extern "C" Mel_Slang_Blob mel_slang_compile(const char* source, const char* entry, Mel_Slang_Stage stage, Mel_Slang_Target target)
{
    (void)stage; // entry-point stage is carried by its [shader("...")] attribute
    Mel_Slang_Blob out = { nullptr, 0, nullptr };

    ComPtr<IGlobalSession> global;
    if (SLANG_FAILED(createGlobalSession(global.writeRef())) || !global)
    {
        out.diagnostics = mel_slang__dup("slang: createGlobalSession failed");
        return out;
    }

    TargetDesc targetDesc = {};
    targetDesc.format = (target == MEL_SLANG_TARGET_MSL) ? SLANG_METAL : SLANG_SPIRV;
    targetDesc.profile = global->findProfile((target == MEL_SLANG_TARGET_MSL) ? "metal" : "spirv_1_5");

    SessionDesc sessionDesc = {};
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;

    ComPtr<ISession> session;
    if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef())) || !session)
    {
        out.diagnostics = mel_slang__dup("slang: createSession failed");
        return out;
    }

    ComPtr<IBlob>   diag;
    ComPtr<IModule> module(session->loadModuleFromSourceString("mel_module", "mel.slang", source, diag.writeRef()));
    if (!module)
    {
        out.diagnostics = mel_slang__dup(mel_slang__diag(diag, "slang: module load failed"));
        return out;
    }

    ComPtr<IEntryPoint> ep;
    if (SLANG_FAILED(module->findEntryPointByName(entry, ep.writeRef())) || !ep)
    {
        out.diagnostics = mel_slang__dup("slang: entry point not found");
        return out;
    }

    IComponentType*           comps[] = { module.get(), ep.get() };
    ComPtr<IComponentType>    composed;
    if (SLANG_FAILED(session->createCompositeComponentType(comps, 2, composed.writeRef(), diag.writeRef())) || !composed)
    {
        out.diagnostics = mel_slang__dup(mel_slang__diag(diag, "slang: compose failed"));
        return out;
    }

    ComPtr<IComponentType> linked;
    if (SLANG_FAILED(composed->link(linked.writeRef(), diag.writeRef())) || !linked)
    {
        out.diagnostics = mel_slang__dup(mel_slang__diag(diag, "slang: link failed"));
        return out;
    }

    ComPtr<IBlob> code;
    if (SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diag.writeRef())) || !code)
    {
        out.diagnostics = mel_slang__dup(mel_slang__diag(diag, "slang: code generation failed"));
        return out;
    }

    size_t n = code->getBufferSize();
    bool   text = (target == MEL_SLANG_TARGET_MSL);
    out.data = malloc(n + (text ? 1 : 0));
    if (!out.data)
    {
        out.diagnostics = mel_slang__dup("slang: out of memory");
        return out;
    }
    memcpy(out.data, code->getBufferPointer(), n);
    if (text)
        ((char*)out.data)[n] = 0;
    out.size = n;
    return out;
}

extern "C" void mel_slang_blob_free(Mel_Slang_Blob* blob)
{
    if (!blob)
        return;
    free(blob->data);
    free(blob->diagnostics);
    blob->data = nullptr;
    blob->diagnostics = nullptr;
    blob->size = 0;
}
