#pragma once

#include <gpu/types.h>

// Shader source is supplied per backend until a single cross-compiled source
// (Slang) is integrated. A given build compiles one backend, so only that
// backend's field is consulted.
typedef struct {
    const char* metal_source;        // MSL,  used by the metal backend
    const char* wgsl_source;         // WGSL, used by the webgpu backend
    const void* spirv_vertex;        // SPIR-V vertex module,   used by the vulkan backend
    usize       spirv_vertex_size;   // bytes
    const void* spirv_fragment;      // SPIR-V fragment module, used by the vulkan backend
    usize       spirv_fragment_size; // bytes
    const char* vertex_entry;        // vertex function/entry name
    const char* fragment_entry;      // fragment function/entry name
} Mel_Gpu_Shader_Opt;

Mel_Gpu_Shader* mel_gpu_shader_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Opt opt);
#define mel_gpu_shader_create(dev, ...) \
    mel_gpu_shader_create_opt((dev), (Mel_Gpu_Shader_Opt){__VA_ARGS__})

void mel_gpu_shader_destroy(Mel_Gpu_Shader* sh);
