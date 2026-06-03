#include "d3d_backend.h"

#include <gpu/shader.h>
#include <log/log.h>

#include <string.h>

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

    if (opt.target != MEL_GPU_SHADER_TARGET_DXIL && opt.target != MEL_GPU_SHADER_TARGET_SPIRV)
    {
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        mel_log_error("gpu", "shader_create_from_bytecode: D3D12 consumes DXIL only, got target %d", (int)opt.target);
        return res;
    }

    const void* vertex = opt.vertex_blob ? opt.vertex_blob : opt.spirv_vertex;
    usize       vertex_size = opt.vertex_blob ? opt.vertex_blob_size : opt.spirv_vertex_size;
    const void* fragment = opt.fragment_blob ? opt.fragment_blob : opt.spirv_fragment;
    usize       fragment_size = opt.fragment_blob ? opt.fragment_blob_size : opt.spirv_fragment_size;

    if (!dev || !vertex || vertex_size == 0 || !fragment || fragment_size == 0)
    {
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        mel_log_error("gpu", "shader_create_from_bytecode: missing DXIL blob(s)");
        return res;
    }

    Mel_Gpu_Shader_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.is_compute = false;
    obj.vs = mel_gpu__dup_blob(dev->alloc, vertex, vertex_size);
    obj.vs_size = vertex_size;
    obj.fs = mel_gpu__dup_blob(dev->alloc, fragment, fragment_size);
    obj.fs_size = fragment_size;

    mel_gpu__dxil_reflect_inputs(obj.vs, obj.vs_size, dev->alloc, &obj.inputs, &obj.input_count, &obj.vertex_stride);

    res.value.slot = mel_gpu__table_insert(dev, &dev->shaders, &obj);
    return res;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_compute_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Compute_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };

    if (opt.target != MEL_GPU_SHADER_TARGET_DXIL && opt.target != MEL_GPU_SHADER_TARGET_SPIRV)
    {
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        mel_log_error("gpu", "shader_create_compute_from_bytecode: D3D12 consumes DXIL only, got target %d", (int)opt.target);
        return res;
    }

    const void* compute = opt.compute_blob ? opt.compute_blob : opt.spirv;
    usize       compute_size = opt.compute_blob ? opt.compute_blob_size : opt.spirv_size;

    if (!dev || !compute || compute_size == 0)
    {
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        mel_log_error("gpu", "shader_create_compute_from_bytecode: missing DXIL blob");
        return res;
    }

    Mel_Gpu_Shader_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.is_compute = true;
    obj.cs = mel_gpu__dup_blob(dev->alloc, compute, compute_size);
    obj.cs_size = compute_size;

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
