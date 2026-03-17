#pragma once

#include "gpu.types.h"
#include "gpu.device.fwd.h"
#include "gpu.descriptor.h"
#include "gpu.shader.fwd.h"
#include "gpu.cmd.fwd.h"
#include "gpu.buffer.fwd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEL_GPU_BLEND_NONE      0
#define MEL_GPU_BLEND_ALPHA     1
#define MEL_GPU_BLEND_ADD       2
#define MEL_GPU_BLEND_MULTIPLY  3

#define MEL_GPU_CULL_NONE       0
#define MEL_GPU_CULL_BACK       1
#define MEL_GPU_CULL_FRONT      2

#define MEL_GPU_TOPOLOGY_TRIANGLE_LIST  0
#define MEL_GPU_TOPOLOGY_TRIANGLE_STRIP 1
#define MEL_GPU_TOPOLOGY_LINE_LIST      2
#define MEL_GPU_TOPOLOGY_POINT_LIST     3

#define MEL_GPU_PIPELINE_GRAPHICS 0
#define MEL_GPU_PIPELINE_COMPUTE  1
#define MEL_GPU_PIPELINE_MESH     2

typedef struct Mel_Gpu_Vertex_Binding {
    u32 binding;
    u32 stride;
    u32 input_rate;
} Mel_Gpu_Vertex_Binding;

typedef struct Mel_Gpu_Vertex_Attribute {
    u32 location;
    u32 binding;
    Mel_Gpu_Format format;
    u32 offset;
} Mel_Gpu_Vertex_Attribute;

typedef struct Mel_Gpu_Pipeline Mel_Gpu_Pipeline;

struct Mel_Gpu_Pipeline {
    void* _pipeline;
    void* _layout;
    u32 _bind_point;
    void* _descriptor_layout;
    void* _descriptor_pool;
    void** _descriptor_pools;
    u32 descriptor_pool_count;
    u32 descriptor_pool_capacity;
    u32 descriptor_pool_max_sets;
    Mel_Gpu_Descriptor_Binding* descriptor_bindings;
    u32 descriptor_binding_count;
    u64 hash;
};

typedef struct {
    Mel_Gpu_Shader* shader;
    Mel_Gpu_Vertex_Binding* bindings;
    u32 binding_count;
    Mel_Gpu_Vertex_Attribute* attributes;
    u32 attribute_count;
    Mel_Gpu_Format color_format;
    Mel_Gpu_Format* color_formats;
    u32 color_format_count;
    Mel_Gpu_Format depth_format;
    u32 blend_mode;
    u32 cull_mode;
    u32 topology;
    u32 pipeline_type;
    bool depth_test;
    bool depth_write;
    u32 push_constant_size;
    Mel_Gpu_Shader_Stage push_constant_stages;
    bool use_texture;
    bool dynamic_cull_mode;
    u32 max_descriptor_sets;
    Mel_Gpu_Descriptor_Binding* descriptor_bindings;
    u32 descriptor_binding_count;
} Mel_Gpu_Pipeline_Opt;

void mel_gpu_pipeline_init_opt(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt);
#define mel_gpu_pipeline_init(pipeline, dev, ...) mel_gpu_pipeline_init_opt((pipeline), (dev), (Mel_Gpu_Pipeline_Opt){__VA_ARGS__})

void mel_gpu_pipeline_shutdown(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev);

void mel_gpu_pipeline_bind(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Cmd* cmd);

void* mel_gpu_pipeline_alloc_descriptor(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev);
void mel_gpu_pipeline_write_texture(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev,
                                    void* set, void* view, void* sampler);
void mel_gpu_pipeline_write_texture_binding(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev,
                                            void* set, u32 binding, void* view, void* sampler);
void mel_gpu_pipeline_write_buffer_binding(Mel_Gpu_Pipeline* pipeline, Mel_Gpu_Device* dev,
                                           void* set, u32 binding, Mel_Gpu_Buffer* buffer,
                                           u64 offset, u64 range, u32 type);

#ifdef __cplusplus
}
#endif
