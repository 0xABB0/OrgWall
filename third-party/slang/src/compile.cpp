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

static Mel_Slang_Blob mel_slang__fail(const char* msg)
{
    Mel_Slang_Blob out = { nullptr, 0, mel_slang__dup(msg) };
    return out;
}

struct Mel_Slang__Target_Map
{
    SlangCompileTarget format;
    const char*        profile;
    bool               text;
    bool               available;
};

static Mel_Slang__Target_Map mel_slang__map_target(Mel_Slang_Target target)
{
    switch (target)
    {
        case MEL_SLANG_TARGET_SPIRV:
            return { SLANG_SPIRV, "spirv_1_5", false, true };
        case MEL_SLANG_TARGET_MSL:
            return { SLANG_METAL, "metal", true, true };
        case MEL_SLANG_TARGET_WGSL:
            return { SLANG_WGSL, "wgsl", true, true };
        case MEL_SLANG_TARGET_DXIL:
#if defined(MEL_SLANG_EMIT_DXIL)
            return { SLANG_DXIL, "sm_6_6", false, true };
#else
            return { SLANG_SPIRV, "spirv_1_5", false, false };
#endif
    }
    return { SLANG_SPIRV, "spirv_1_5", false, false };
}

static Mel_Slang_Vertex_Format mel_slang__vertex_format(TypeReflection::ScalarType scalar, unsigned components)
{
    if (components < 1 || components > 4)
        return MEL_SLANG_FORMAT_UNKNOWN;
    switch (scalar)
    {
        case TypeReflection::Float32:
            return (Mel_Slang_Vertex_Format)(MEL_SLANG_FORMAT_F32 + (components - 1));
        case TypeReflection::Int32:
            return (Mel_Slang_Vertex_Format)(MEL_SLANG_FORMAT_I32 + (components - 1));
        case TypeReflection::UInt32:
            return (Mel_Slang_Vertex_Format)(MEL_SLANG_FORMAT_U32 + (components - 1));
        default:
            return MEL_SLANG_FORMAT_UNKNOWN;
    }
}

static uint32_t mel_slang__format_size(Mel_Slang_Vertex_Format f)
{
    switch (f)
    {
        case MEL_SLANG_FORMAT_F32:
        case MEL_SLANG_FORMAT_I32:
        case MEL_SLANG_FORMAT_U32:
            return 4;
        case MEL_SLANG_FORMAT_F32X2:
        case MEL_SLANG_FORMAT_I32X2:
        case MEL_SLANG_FORMAT_U32X2:
            return 8;
        case MEL_SLANG_FORMAT_F32X3:
        case MEL_SLANG_FORMAT_I32X3:
        case MEL_SLANG_FORMAT_U32X3:
            return 12;
        case MEL_SLANG_FORMAT_F32X4:
        case MEL_SLANG_FORMAT_I32X4:
        case MEL_SLANG_FORMAT_U32X4:
            return 16;
        case MEL_SLANG_FORMAT_UNKNOWN:
            return 0;
    }
    return 0;
}

static unsigned mel_slang__component_count(TypeReflection* type)
{
    if (!type)
        return 0;
    switch (type->getKind())
    {
        case TypeReflection::Kind::Scalar:
            return 1;
        case TypeReflection::Kind::Vector:
            return (unsigned)type->getElementCount();
        default:
            return 0;
    }
}

static TypeReflection::ScalarType mel_slang__scalar_of(TypeReflection* type)
{
    if (!type)
        return TypeReflection::None;
    if (type->getKind() == TypeReflection::Kind::Vector)
        return type->getElementType() ? type->getElementType()->getScalarType() : TypeReflection::None;
    return type->getScalarType();
}

static void mel_slang__collect_vertex_attr(VariableLayoutReflection* field, Mel_Slang_Vertex_Attr** attrs, uint32_t* count, uint32_t* offset)
{
    TypeLayoutReflection* tl = field->getTypeLayout();
    if (!tl)
        return;
    TypeReflection*      type = tl->getType();
    TypeReflection::Kind kind = type ? type->getKind() : TypeReflection::Kind::None;

    if (kind == TypeReflection::Kind::Struct)
    {
        unsigned fc = tl->getFieldCount();
        for (unsigned i = 0; i < fc; ++i)
            mel_slang__collect_vertex_attr(tl->getFieldByIndex(i), attrs, count, offset);
        return;
    }

    unsigned                   comps  = mel_slang__component_count(type);
    TypeReflection::ScalarType scalar = mel_slang__scalar_of(type);
    Mel_Slang_Vertex_Format    fmt    = mel_slang__vertex_format(scalar, comps);

    uint32_t n = *count + 1;
    *attrs     = (Mel_Slang_Vertex_Attr*)realloc(*attrs, n * sizeof(Mel_Slang_Vertex_Attr));
    Mel_Slang_Vertex_Attr* a = &(*attrs)[*count];
    a->semantic              = mel_slang__dup(field->getSemanticName());
    a->location              = (uint32_t)field->getOffset(SLANG_PARAMETER_CATEGORY_VARYING_INPUT);
    a->format                = fmt;
    a->size                  = mel_slang__format_size(fmt);
    a->offset                = *offset;
    *offset += a->size;
    *count = n;
}

static Mel_Slang_Resource_Kind mel_slang__resource_kind(TypeLayoutReflection* tl)
{
    TypeReflection* type = tl->getType();
    if (!type)
        return MEL_SLANG_RESOURCE_UNKNOWN;
    switch (type->getKind())
    {
        case TypeReflection::Kind::ConstantBuffer:
            return MEL_SLANG_RESOURCE_UNIFORM_BUFFER;
        case TypeReflection::Kind::SamplerState:
            return MEL_SLANG_RESOURCE_SAMPLER;
        case TypeReflection::Kind::ShaderStorageBuffer:
            return MEL_SLANG_RESOURCE_STORAGE_BUFFER;
        case TypeReflection::Kind::Resource:
        {
            SlangResourceShape  shape  = (SlangResourceShape)(type->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK);
            SlangResourceAccess access = type->getResourceAccess();
            if (shape == SLANG_STRUCTURED_BUFFER || shape == SLANG_BYTE_ADDRESS_BUFFER)
                return MEL_SLANG_RESOURCE_STORAGE_BUFFER;
            if (access == SLANG_RESOURCE_ACCESS_READ_WRITE || access == SLANG_RESOURCE_ACCESS_WRITE)
                return MEL_SLANG_RESOURCE_STORAGE_TEXTURE;
            return MEL_SLANG_RESOURCE_SAMPLED_TEXTURE;
        }
        default:
            return MEL_SLANG_RESOURCE_UNKNOWN;
    }
}

static void mel_slang__collect_binding(VariableLayoutReflection* param, Mel_Slang_Reflection* refl)
{
    TypeLayoutReflection* tl = param->getTypeLayout();
    if (!tl)
        return;

    bool     isPush = false;
    unsigned cats   = tl->getCategoryCount();
    for (unsigned i = 0; i < cats; ++i)
    {
        if (tl->getCategoryByIndex(i) == PushConstantBuffer)
        {
            isPush = true;
            break;
        }
    }
    if (cats == 0 && tl->getParameterCategory() == PushConstantBuffer)
        isPush = true;

    if (isPush)
    {
        TypeLayoutReflection* elem = tl->getElementTypeLayout();
        size_t                sz   = elem ? elem->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM) : tl->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
        refl->push_constant_size   = (uint32_t)sz;
        return;
    }

    Mel_Slang_Resource_Kind kind = mel_slang__resource_kind(tl->unwrapArray());
    if (kind == MEL_SLANG_RESOURCE_UNKNOWN)
        return;

    size_t count = 1;
    if (tl->isArray())
        count = tl->getElementCount();
    if (count == (size_t)SLANG_UNBOUNDED_SIZE)
        count = 0;

    uint32_t size = 0;
    if (kind == MEL_SLANG_RESOURCE_UNIFORM_BUFFER)
    {
        TypeLayoutReflection* elem = tl->getElementTypeLayout();
        if (elem)
            size = (uint32_t)elem->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
    }

    uint32_t n     = refl->binding_count + 1;
    refl->bindings = (Mel_Slang_Resource_Binding*)realloc(refl->bindings, n * sizeof(Mel_Slang_Resource_Binding));
    Mel_Slang_Resource_Binding* b = &refl->bindings[refl->binding_count];
    b->name             = mel_slang__dup(param->getName());
    b->kind             = kind;
    b->set              = param->getBindingSpace() + (uint32_t)param->getOffset(SLANG_PARAMETER_CATEGORY_REGISTER_SPACE);
    b->slot             = param->getBindingIndex();
    b->count            = (uint32_t)count;
    b->size             = size;
    refl->binding_count = n;
}

static void mel_slang__reflect(IComponentType* linked, const char* entry, Mel_Slang_Reflection* out)
{
    memset(out, 0, sizeof(*out));

    ProgramLayout* layout = linked->getLayout(0, nullptr);
    if (!layout)
        return;

    EntryPointReflection* ep      = nullptr;
    SlangUInt             epCount = layout->getEntryPointCount();
    for (SlangUInt i = 0; i < epCount; ++i)
    {
        EntryPointReflection* cand = layout->getEntryPointByIndex(i);
        const char*           name = cand ? cand->getName() : nullptr;
        if (name && entry && strcmp(name, entry) == 0)
        {
            ep = cand;
            break;
        }
    }
    if (!ep && epCount > 0)
        ep = layout->getEntryPointByIndex(0);
    if (!ep)
        return;

    out->entry = mel_slang__dup(ep->getName());

    SlangStage stage = ep->getStage();
    if (stage == SLANG_STAGE_VERTEX)
        out->stage = MEL_SLANG_STAGE_VERTEX;
    else if (stage == SLANG_STAGE_FRAGMENT)
        out->stage = MEL_SLANG_STAGE_FRAGMENT;
    else if (stage == SLANG_STAGE_COMPUTE)
        out->stage = MEL_SLANG_STAGE_COMPUTE;

    if (stage == SLANG_STAGE_COMPUTE)
    {
        out->is_compute = 1;
        SlangUInt sz[3] = { 0, 0, 0 };
        ep->getComputeThreadGroupSize(3, sz);
        out->workgroup[0] = (uint32_t)sz[0];
        out->workgroup[1] = (uint32_t)sz[1];
        out->workgroup[2] = (uint32_t)sz[2];
    }

    if (stage == SLANG_STAGE_VERTEX)
    {
        uint32_t offset = 0;
        unsigned pc     = ep->getParameterCount();
        for (unsigned i = 0; i < pc; ++i)
            mel_slang__collect_vertex_attr(ep->getParameterByIndex(i), &out->vertex_attrs, &out->vertex_attr_count, &offset);
        out->vertex_stride = offset;
    }

    unsigned gpc = layout->getParameterCount();
    for (unsigned i = 0; i < gpc; ++i)
        mel_slang__collect_binding(layout->getParameterByIndex(i), out);
}

extern "C" Mel_Slang_Blob mel_slang_compile_reflect(const char* source, const char* entry, Mel_Slang_Stage stage, Mel_Slang_Target target, Mel_Slang_Reflection* out_reflection)
{
    (void)stage;
    if (out_reflection)
        memset(out_reflection, 0, sizeof(*out_reflection));

    Mel_Slang__Target_Map map = mel_slang__map_target(target);
    if (!map.available)
    {
        if (target == MEL_SLANG_TARGET_DXIL)
            return mel_slang__fail("slang: DXIL unavailable off-win32 (D3D12 + dxcompiler/dxil signers are win32-only)");
        return mel_slang__fail("slang: emit target not available on this platform");
    }

    ComPtr<IGlobalSession> global;
    if (SLANG_FAILED(createGlobalSession(global.writeRef())) || !global)
        return mel_slang__fail("slang: createGlobalSession failed");

    TargetDesc targetDesc = {};
    targetDesc.format     = map.format;
    targetDesc.profile    = global->findProfile(map.profile);

    SessionDesc sessionDesc = {};
    sessionDesc.targets     = &targetDesc;
    sessionDesc.targetCount = 1;

    ComPtr<ISession> session;
    if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef())) || !session)
        return mel_slang__fail("slang: createSession failed");

    ComPtr<IBlob>   diag;
    ComPtr<IModule> module(session->loadModuleFromSourceString("mel_module", "mel.slang", source, diag.writeRef()));
    if (!module)
        return mel_slang__fail(mel_slang__diag(diag, "slang: module load failed"));

    ComPtr<IEntryPoint> ep;
    if (SLANG_FAILED(module->findEntryPointByName(entry, ep.writeRef())) || !ep)
        return mel_slang__fail("slang: entry point not found");

    IComponentType*        comps[] = { module.get(), ep.get() };
    ComPtr<IComponentType> composed;
    if (SLANG_FAILED(session->createCompositeComponentType(comps, 2, composed.writeRef(), diag.writeRef())) || !composed)
        return mel_slang__fail(mel_slang__diag(diag, "slang: compose failed"));

    ComPtr<IComponentType> linked;
    if (SLANG_FAILED(composed->link(linked.writeRef(), diag.writeRef())) || !linked)
        return mel_slang__fail(mel_slang__diag(diag, "slang: link failed"));

    ComPtr<IBlob> code;
    if (SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diag.writeRef())) || !code)
        return mel_slang__fail(mel_slang__diag(diag, "slang: code generation failed"));

    if (out_reflection)
        mel_slang__reflect(linked.get(), entry, out_reflection);

    Mel_Slang_Blob out  = { nullptr, 0, nullptr };
    size_t         n    = code->getBufferSize();
    bool           text = map.text;
    out.data            = malloc(n + (text ? 1 : 0));
    if (!out.data)
    {
        if (out_reflection)
            mel_slang_reflection_free(out_reflection);
        return mel_slang__fail("slang: out of memory");
    }
    memcpy(out.data, code->getBufferPointer(), n);
    if (text)
        ((char*)out.data)[n] = 0;
    out.size = n;
    return out;
}

extern "C" Mel_Slang_Blob mel_slang_compile(const char* source, const char* entry, Mel_Slang_Stage stage, Mel_Slang_Target target)
{
    return mel_slang_compile_reflect(source, entry, stage, target, nullptr);
}

extern "C" void mel_slang_blob_free(Mel_Slang_Blob* blob)
{
    if (!blob)
        return;
    free(blob->data);
    free(blob->diagnostics);
    blob->data        = nullptr;
    blob->diagnostics = nullptr;
    blob->size        = 0;
}

extern "C" void mel_slang_reflection_free(Mel_Slang_Reflection* reflection)
{
    if (!reflection)
        return;
    for (uint32_t i = 0; i < reflection->vertex_attr_count; ++i)
        free(reflection->vertex_attrs[i].semantic);
    free(reflection->vertex_attrs);
    for (uint32_t i = 0; i < reflection->binding_count; ++i)
        free(reflection->bindings[i].name);
    free(reflection->bindings);
    free(reflection->entry);
    memset(reflection, 0, sizeof(*reflection));
}
