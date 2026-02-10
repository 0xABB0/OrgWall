#ifndef MEL_GPU_SHADER_H
#define MEL_GPU_SHADER_H

#include "gpu.device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER  0
#define MEL_GPU_DESCRIPTOR_STORAGE_BUFFER  1
#define MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE   2
#define MEL_GPU_DESCRIPTOR_STORAGE_IMAGE   3
#define MEL_GPU_DESCRIPTOR_SAMPLER         4

typedef struct {
    u32 set;
    u32 binding;
    u32 descriptor_type;
    u32 count;
} Mel_Gpu_Shader_Binding;

typedef struct Mel_Gpu_Shader Mel_Gpu_Shader;

struct Mel_Gpu_Shader {
    VkShaderModule vertex;
    VkShaderModule fragment;
    Mel_Gpu_Shader_Binding* bindings;
    u32 binding_count;
    VkPushConstantRange* push_ranges;
    u32 push_range_count;
    const Mel_Alloc* alloc;
};

typedef struct {
    str8 source;
    str8 vertex_entry;
    str8 fragment_entry;
} Mel_Gpu_Shader_Opt;

void mel_gpu_shader_init_opt(Mel_Gpu_Shader* shader, Mel_Gpu_Device* dev, Mel_Gpu_Shader_Opt opt);
#define mel_gpu_shader_init(shader, dev, ...) mel_gpu_shader_init_opt((shader), (dev), (Mel_Gpu_Shader_Opt){__VA_ARGS__})

void mel_gpu_shader_shutdown(Mel_Gpu_Shader* shader, Mel_Gpu_Device* dev);

bool mel_slang_init(void);
void mel_slang_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
