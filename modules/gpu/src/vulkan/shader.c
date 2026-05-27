#include "vulkan_backend.h"

static char* dup_cstr(const char* s)
{
    const char* src = s ? s : "main";
    usize n = strlen(src) + 1;
    char* d = malloc(n);
    if (d) memcpy(d, src, n);
    return d;
}

static VkShaderModule make_module(VkDevice device, const void* code, usize size)
{
    VkShaderModuleCreateInfo ci = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode    = (const u32*)code,
    };
    VkShaderModule m;
    if (vkCreateShaderModule(device, &ci, NULL, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

Mel_Gpu_Shader* mel_gpu_shader_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Opt opt)
{
    if (!dev || !opt.spirv_vertex || !opt.spirv_fragment) return NULL;

    VkShaderModule vs = make_module(dev->device, opt.spirv_vertex, opt.spirv_vertex_size);
    VkShaderModule fs = make_module(dev->device, opt.spirv_fragment, opt.spirv_fragment_size);
    if (!vs || !fs) {
        if (vs) vkDestroyShaderModule(dev->device, vs, NULL);
        if (fs) vkDestroyShaderModule(dev->device, fs, NULL);
        return NULL;
    }

    Mel_Gpu_Shader* sh = calloc(1, sizeof *sh);
    if (!sh) return NULL;
    sh->device         = dev;
    sh->vs             = vs;
    sh->fs             = fs;
    sh->vertex_entry   = dup_cstr(opt.vertex_entry);
    sh->fragment_entry = dup_cstr(opt.fragment_entry);
    return sh;
}

void mel_gpu_shader_destroy(Mel_Gpu_Shader* sh)
{
    if (!sh) return;
    vkDestroyShaderModule(sh->device->device, sh->vs, NULL);
    vkDestroyShaderModule(sh->device->device, sh->fs, NULL);
    free(sh->vertex_entry);
    free(sh->fragment_entry);
    free(sh);
}
