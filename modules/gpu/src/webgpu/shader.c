#include "wgpu_backend.h"

#include <allocator/heap.h>
#include <log/log.h>

#include <string.h>

static char* mel_gpu__dup_entry(const Mel_Alloc* a, const char* e, const char* fallback)
{
    const char* src = e ? e : fallback;
    usize       n = strlen(src);
    char*       out = mel_alloc(a, n + 1);
    memcpy(out, src, n + 1);
    return out;
}

static WGPUShaderModule mel_gpu__module_wgsl(Mel_Gpu_Device* dev, const void* blob, usize size, const char* name)
{
    WGPUShaderSourceWGSL src = {
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = (const char*)blob, .length = size },
    };
    WGPUShaderModuleDescriptor desc = { .nextInChain = &src.chain, .label = mel_gpu__sv(name) };
    return wgpuDeviceCreateShaderModule(dev->wgpu, &desc);
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Bytecode_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };

    if (opt.target != MEL_GPU_SHADER_TARGET_WGSL)
    {
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        mel_log_error("gpu",
                      "shader_create_from_bytecode: target %d unsupported on the WebGPU backend — the vendored Dawn Release "
                      "prebuilt has the SPIR-V reader compiled out (caps.shader.bytecode_passthrough.spirv=false); author to WGSL "
                      "(MEL_GPU_SHADER_TARGET_WGSL). Shader '%s' refused.",
                      (int)opt.target, opt.name ? opt.name : "(unnamed)");
        return res;
    }

    if (!dev || !dev->caps.shader.bytecode_passthrough.wgsl)
    {
        mel_log_error("gpu", "shader_create_from_bytecode: device reports caps.shader.bytecode_passthrough.wgsl=false; refusing WGSL bytecode for '%s'", opt.name ? opt.name : "(unnamed)");
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        return res;
    }

    bool have_vertex = opt.vertex_blob && opt.vertex_blob_size;
    bool have_fragment = opt.fragment_blob && opt.fragment_blob_size;
    if (!have_vertex || !have_fragment)
    {
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        mel_log_error("gpu", "shader_create_from_bytecode: missing vertex or fragment blob for '%s'", opt.name ? opt.name : "(unnamed)");
        return res;
    }

    WGPUShaderModule vmod = mel_gpu__module_wgsl(dev, opt.vertex_blob, opt.vertex_blob_size, opt.name);
    WGPUShaderModule fmod = mel_gpu__module_wgsl(dev, opt.fragment_blob, opt.fragment_blob_size, opt.name);
    if (!vmod || !fmod)
    {
        if (vmod)
            wgpuShaderModuleRelease(vmod);
        if (fmod)
            wgpuShaderModuleRelease(fmod);
        res.status = MEL_GPU_SHADER_CREATE_BACKEND_FAILED;
        mel_log_error("gpu", "shader_create_from_bytecode: wgpuDeviceCreateShaderModule failed for '%s'", opt.name ? opt.name : "(unnamed)");
        return res;
    }

    Mel_Gpu_Shader_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .name = opt.name },
        .vertex = vmod,
        .fragment = fmod,
        .vertex_entry = mel_gpu__dup_entry(dev->alloc, opt.vertex_entry, "vs_main"),
        .fragment_entry = mel_gpu__dup_entry(dev->alloc, opt.fragment_entry, "fs_main"),
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->shaders, &o);
    res.value = (Mel_Gpu_Shader){ h };
    return res;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_compute_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Compute_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };

    if (opt.target != MEL_GPU_SHADER_TARGET_WGSL)
    {
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        mel_log_error("gpu",
                      "shader_create_compute_from_bytecode: target %d unsupported on the WebGPU backend — the vendored Dawn "
                      "Release prebuilt has no SPIR-V reader; author to WGSL. Shader '%s' refused.",
                      (int)opt.target, opt.name ? opt.name : "(unnamed)");
        return res;
    }

    if (!dev || !dev->caps.shader.bytecode_passthrough.wgsl)
    {
        mel_log_error("gpu", "shader_create_compute_from_bytecode: device reports caps.shader.bytecode_passthrough.wgsl=false; refusing WGSL bytecode for '%s'", opt.name ? opt.name : "(unnamed)");
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        return res;
    }

    if (!opt.compute_blob || !opt.compute_blob_size)
    {
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        mel_log_error("gpu", "shader_create_compute_from_bytecode: missing WGSL compute blob for '%s'", opt.name ? opt.name : "(unnamed)");
        return res;
    }

    WGPUShaderModule cmod = mel_gpu__module_wgsl(dev, opt.compute_blob, opt.compute_blob_size, opt.name);
    if (!cmod)
    {
        res.status = MEL_GPU_SHADER_CREATE_BACKEND_FAILED;
        mel_log_error("gpu", "shader_create_compute_from_bytecode: wgpuDeviceCreateShaderModule failed for '%s'", opt.name ? opt.name : "(unnamed)");
        return res;
    }

    Mel_Gpu_Shader_Obj o = {
        .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .name = opt.name },
        .compute = cmod,
        .compute_entry = mel_gpu__dup_entry(dev->alloc, opt.entry, "main"),
    };
    Mel_SlotMap_Handle h = mel_gpu__table_insert(dev, &dev->shaders, &o);
    res.value = (Mel_Gpu_Shader){ h };
    return res;
}

void mel_gpu_shader_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh)
{
    Mel_Gpu_Shader_Obj* o = mel_gpu__table_get(dev, &dev->shaders, sh.slot);
    if (!o)
        return;
    if (o->vertex)
        wgpuShaderModuleRelease(o->vertex);
    if (o->fragment)
        wgpuShaderModuleRelease(o->fragment);
    if (o->compute)
        wgpuShaderModuleRelease(o->compute);
    if (o->vertex_entry)
        mel_dealloc(dev->alloc, o->vertex_entry);
    if (o->fragment_entry)
        mel_dealloc(dev->alloc, o->fragment_entry);
    if (o->compute_entry)
        mel_dealloc(dev->alloc, o->compute_entry);
    mel_gpu__table_remove(dev, &dev->shaders, sh.slot);
}

bool mel_gpu_shader_alive(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh) { return mel_gpu__table_alive(dev, &dev->shaders, sh.slot); }
