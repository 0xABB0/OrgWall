#pragma once

#include "gpu.types.h"
#include "gpu.device.fwd.h"
#include "gpu.cmd.fwd.h"
#include "gpu.buffer.fwd.h"

#define MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER  0
#define MEL_GPU_DESCRIPTOR_STORAGE_BUFFER  1
#define MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE   2
#define MEL_GPU_DESCRIPTOR_STORAGE_IMAGE   3
#define MEL_GPU_DESCRIPTOR_SAMPLER         4
#define MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER 5

typedef struct Mel_Gpu_Descriptor_Binding Mel_Gpu_Descriptor_Binding;
typedef struct Mel_Gpu_Descriptor_Layout Mel_Gpu_Descriptor_Layout;
typedef struct Mel_Gpu_Descriptor_Pool Mel_Gpu_Descriptor_Pool;

#define MEL_GPU_DESCRIPTOR_BINDING_PARTIALLY_BOUND    (1u << 0)
#define MEL_GPU_DESCRIPTOR_BINDING_VARIABLE_COUNT     (1u << 1)
#define MEL_GPU_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND  (1u << 2)

struct Mel_Gpu_Descriptor_Binding {
    u32 binding;
    u32 type;
    u32 count;
    Mel_Gpu_Shader_Stage stages;
    u32 flags;
};

struct Mel_Gpu_Descriptor_Layout {
    void* _layout;
    u32 binding_count;
};

struct Mel_Gpu_Descriptor_Pool {
    void* _pool;
    void* _layout;
    u32 max_sets;
    u32 variable_count;
};

typedef struct {
    Mel_Gpu_Descriptor_Binding* bindings;
    u32 binding_count;
} Mel_Gpu_Descriptor_Layout_Opt;

void mel_gpu_descriptor_layout_init_opt(Mel_Gpu_Descriptor_Layout* dl, Mel_Gpu_Device* dev, Mel_Gpu_Descriptor_Layout_Opt opt);
#define mel_gpu_descriptor_layout_init(dl, dev, ...) mel_gpu_descriptor_layout_init_opt((dl), (dev), (Mel_Gpu_Descriptor_Layout_Opt){__VA_ARGS__})

void mel_gpu_descriptor_layout_shutdown(Mel_Gpu_Descriptor_Layout* dl, Mel_Gpu_Device* dev);

typedef struct {
    Mel_Gpu_Descriptor_Layout* layout;
    u32 max_sets;
    bool update_after_bind;
    u32 variable_count;
} Mel_Gpu_Descriptor_Pool_Opt;

void mel_gpu_descriptor_pool_init_opt(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev, Mel_Gpu_Descriptor_Pool_Opt opt);
#define mel_gpu_descriptor_pool_init(dp, dev, ...) mel_gpu_descriptor_pool_init_opt((dp), (dev), (Mel_Gpu_Descriptor_Pool_Opt){__VA_ARGS__})

void mel_gpu_descriptor_pool_shutdown(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev);

void* mel_gpu_descriptor_pool_alloc(Mel_Gpu_Descriptor_Pool* dp, Mel_Gpu_Device* dev);

void mel_gpu_descriptor_write_texture(Mel_Gpu_Device* dev, void* set,
                                      u32 binding, void* view, void* sampler);

void mel_gpu_descriptor_write_buffer(Mel_Gpu_Device* dev, void* set,
                                     u32 binding, Mel_Gpu_Buffer* buffer, u64 offset, u64 range,
                                     u32 type);

void mel_gpu_descriptor_bind(Mel_Gpu_Cmd* cmd, void* pipeline_layout, void* set);
