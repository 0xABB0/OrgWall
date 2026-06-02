#include "d3d_backend.h"

#include <gpu/shader.h>
#include <log/log.h>

#include <string.h>

// U12 raw-bytecode passthrough (gpu-rhi.md §6.4). The public Mel_Gpu_Shader_Bytecode_Opt fields are
// SPIR-V-named; on D3D12 they carry DXIL (the public surface is backend-clean — flagged). D3D12_SHADER_BYTECODE
// carries no entry name (the entry is baked at dxc compile time via -E), so vertex_entry / fragment_entry are
// ignored on D3D12. The blobs are copied so the caller may free them; reflection extracts the vertex input
// signature only (the reflection-default vertex layout, §6.5).

bool mel_gpu__shader_get(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, Mel_Gpu_Shader_Obj** out)
{
    Mel_Gpu_Shader_Obj* o = mel_gpu__table_get(dev, &dev->shaders, sh.slot);
    if (!o)
        return false;
    *out = o;
    return true;
}

static void* mel_gpu__dup_blob(const Mel_Alloc* alloc, const void* src, usize bytes)
{
    if (!src || bytes == 0)
        return NULL;
    void* p = mel_alloc(alloc, bytes);
    memcpy(p, src, bytes);
    return p;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Bytecode_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };

    if (!dev || !opt.spirv_vertex || opt.spirv_vertex_size == 0 || !opt.spirv_fragment || opt.spirv_fragment_size == 0)
    {
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        mel_log_error("gpu", "shader_create_from_bytecode: missing DXIL blob(s)");
        return res;
    }

    Mel_Gpu_Shader_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.is_compute = false;
    obj.vs = mel_gpu__dup_blob(dev->alloc, opt.spirv_vertex, opt.spirv_vertex_size);
    obj.vs_size = opt.spirv_vertex_size;
    obj.fs = mel_gpu__dup_blob(dev->alloc, opt.spirv_fragment, opt.spirv_fragment_size);
    obj.fs_size = opt.spirv_fragment_size;

    mel_gpu__dxil_reflect_inputs(obj.vs, obj.vs_size, dev->alloc, &obj.inputs, &obj.input_count, &obj.vertex_stride);

    res.value.slot = mel_gpu__table_insert(dev, &dev->shaders, &obj);
    return res;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_compute_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Compute_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };

    if (!dev || !opt.spirv || opt.spirv_size == 0)
    {
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        mel_log_error("gpu", "shader_create_compute_from_bytecode: missing DXIL blob");
        return res;
    }

    Mel_Gpu_Shader_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.is_compute = true;
    obj.cs = mel_gpu__dup_blob(dev->alloc, opt.spirv, opt.spirv_size);
    obj.cs_size = opt.spirv_size;

    res.value.slot = mel_gpu__table_insert(dev, &dev->shaders, &obj);
    return res;
}

void mel_gpu_shader_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh)
{
    Mel_Gpu_Shader_Obj* o = mel_gpu__table_get(dev, &dev->shaders, sh.slot);
    if (!o)
        return;
    if (o->vs)
        mel_dealloc(dev->alloc, o->vs);
    if (o->fs)
        mel_dealloc(dev->alloc, o->fs);
    if (o->cs)
        mel_dealloc(dev->alloc, o->cs);
    mel_gpu__dxil_inputs_free(dev->alloc, o->inputs, o->input_count);
    mel_gpu__table_remove(dev, &dev->shaders, sh.slot);
}

bool mel_gpu_shader_alive(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh) { return mel_gpu__table_get(dev, &dev->shaders, sh.slot) != NULL; }
