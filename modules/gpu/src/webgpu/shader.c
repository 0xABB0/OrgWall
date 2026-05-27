#include "webgpu_backend.h"

static char* dup_cstr(const char* s)
{
    if (!s) return NULL;
    usize n = strlen(s) + 1;
    char* d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

Mel_Gpu_Shader* mel_gpu_shader_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Opt opt)
{
    if (!dev || !opt.wgsl_source) return NULL;

    WGPUShaderSourceWGSL wgsl = {
        .chain = { .next = NULL, .sType = WGPUSType_ShaderSourceWGSL },
        .code  = mel_gpu__sv(opt.wgsl_source),
    };
    WGPUShaderModuleDescriptor desc = { .nextInChain = &wgsl.chain };

    WGPUShaderModule module = wgpuDeviceCreateShaderModule(dev->device, &desc);
    if (!module) return NULL;

    Mel_Gpu_Shader* sh = calloc(1, sizeof *sh);
    if (!sh) { wgpuShaderModuleRelease(module); return NULL; }
    sh->module         = module;
    sh->vertex_entry   = dup_cstr(opt.vertex_entry);
    sh->fragment_entry = dup_cstr(opt.fragment_entry);
    return sh;
}

void mel_gpu_shader_destroy(Mel_Gpu_Shader* sh)
{
    if (!sh) return;
    if (sh->module) wgpuShaderModuleRelease(sh->module);
    free(sh->vertex_entry);
    free(sh->fragment_entry);
    free(sh);
}
