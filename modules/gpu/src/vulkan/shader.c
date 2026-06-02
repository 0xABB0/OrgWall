#include "vk_backend.h"

#include <gpu/shader.h>
#include <log/log.h>

#include <stdlib.h>
#include <string.h>

static char* mel_gpu__strdup(const Mel_Alloc* a, const char* s)
{
    if (!s)
        s = "main";
    usize n = strlen(s) + 1;
    char* d = mel_alloc(a, n);
    memcpy(d, s, n);
    return d;
}

static VkShaderModule mel_gpu__make_module(Mel_Gpu_Device* dev, const void* code, usize size)
{
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = (const u32*)code,
    };
    VkShaderModule m = VK_NULL_HANDLE;
    VkResult       r = vkCreateShaderModule(dev->vk, &ci, NULL, &m);
    if (r != VK_SUCCESS)
        mel_log_error("gpu", "vkCreateShaderModule failed: %s", mel_gpu__vk_result_str(r));
    return m;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Bytecode_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };

    if (!dev || !opt.spirv_vertex || !opt.spirv_fragment)
    {
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        return res;
    }

    VkShaderModule vs = mel_gpu__make_module(dev, opt.spirv_vertex, opt.spirv_vertex_size);
    VkShaderModule fs = mel_gpu__make_module(dev, opt.spirv_fragment, opt.spirv_fragment_size);
    if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE)
    {
        if (vs)
            vkDestroyShaderModule(dev->vk, vs, NULL);
        if (fs)
            vkDestroyShaderModule(dev->vk, fs, NULL);
        res.status = MEL_GPU_SHADER_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Shader_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.vs = vs;
    obj.fs = fs;
    obj.vs_entry = mel_gpu__strdup(dev->alloc, opt.vertex_entry);
    obj.fs_entry = mel_gpu__strdup(dev->alloc, opt.fragment_entry);

    obj.reflection = (Mel_Gpu_Spirv_Reflection){ 0 };
    mel_gpu__spirv_reflect((const u32*)opt.spirv_vertex, opt.spirv_vertex_size, true, dev->alloc, &obj.reflection);
    mel_gpu__spirv_reflect((const u32*)opt.spirv_fragment, opt.spirv_fragment_size, false, dev->alloc, &obj.reflection);

    res.value.slot = mel_gpu__table_insert(dev, &dev->shaders, &obj);
    return res;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_compute_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Compute_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };
    if (!dev || !opt.spirv)
    {
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        return res;
    }

    VkShaderModule cs = mel_gpu__make_module(dev, opt.spirv, opt.spirv_size);
    if (cs == VK_NULL_HANDLE)
    {
        res.status = MEL_GPU_SHADER_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Shader_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.cs = cs;
    obj.cs_entry = mel_gpu__strdup(dev->alloc, opt.entry);

    obj.reflection = (Mel_Gpu_Spirv_Reflection){ 0 };
    mel_gpu__spirv_reflect((const u32*)opt.spirv, opt.spirv_size, false, dev->alloc, &obj.reflection);

    res.value.slot = mel_gpu__table_insert(dev, &dev->shaders, &obj);
    return res;
}

void mel_gpu_shader_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh)
{
    const void* trk = mel_gpu__track_key(&dev->shaders, sh.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    Mel_Gpu_Shader_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->shaders, sh.slot, &o))
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    VkShaderModule           vs = o.vs, fs = o.fs, cs = o.cs;
    char *                   ve = o.vs_entry, *fe = o.fs_entry, *ce = o.cs_entry;
    Mel_Gpu_Spirv_Reflection refl = o.reflection;
    mel_gpu__table_remove(dev, &dev->shaders, sh.slot);
    if (vs)
        vkDestroyShaderModule(dev->vk, vs, NULL);
    if (fs)
        vkDestroyShaderModule(dev->vk, fs, NULL);
    if (cs)
        vkDestroyShaderModule(dev->vk, cs, NULL);
    if (ve)
        mel_dealloc(dev->alloc, ve);
    if (fe)
        mel_dealloc(dev->alloc, fe);
    if (ce)
        mel_dealloc(dev->alloc, ce);
    mel_gpu__reflection_free(&refl);
    mel_gpu__track_exit(dev, trk);
}

bool mel_gpu_shader_alive(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh) { return mel_gpu__table_alive(dev, &dev->shaders, sh.slot); }

bool mel_gpu__shader_modules(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, VkShaderModule* vs, VkShaderModule* fs, const char** vs_entry, const char** fs_entry)
{
    Mel_Gpu_Shader_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->shaders, sh.slot, &o))
        return false;
    *vs = o.vs;
    *fs = o.fs;
    *vs_entry = o.vs_entry;
    *fs_entry = o.fs_entry;
    return true;
}

bool mel_gpu__shader_compute_module(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, VkShaderModule* cs, const char** cs_entry)
{
    Mel_Gpu_Shader_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->shaders, sh.slot, &o) || o.cs == VK_NULL_HANDLE)
        return false;
    *cs = o.cs;
    *cs_entry = o.cs_entry;
    return true;
}

bool mel_gpu__shader_reflection(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, Mel_Gpu_Spirv_Reflection* out)
{
    Mel_Gpu_Shader_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->shaders, sh.slot, &o))
        return false;
    *out = o.reflection;
    return true;
}
