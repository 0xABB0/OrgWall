#pragma once

#include "gpu.types.h"
#include "gpu.device.fwd.h"
#include "event.channel.fwd.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"
#include "async.job.fwd.h"
#include "async.signal.fwd.h"

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

typedef struct {
    Mel_Gpu_Shader_Stage stages;
    u32 offset;
    u32 size;
} Mel_Gpu_Push_Constant_Range;

struct Mel_Gpu_Shader {
    void* _vertex;
    void* _fragment;
    void* _compute;
    void* _task;
    void* _mesh;
    Mel_Gpu_Shader_Binding* bindings;
    u32 binding_count;
    Mel_Gpu_Push_Constant_Range* push_ranges;
    u32 push_range_count;
    const Mel_Alloc* alloc;
};

typedef struct {
    str8 source;
    str8 vertex_entry;
    str8 fragment_entry;
    str8 compute_entry;
    str8 task_entry;
    str8 mesh_entry;
} Mel_Gpu_Shader_Opt;

void mel_gpu_shader_init_opt(Mel_Gpu_Shader* shader, Mel_Gpu_Device* dev, Mel_Gpu_Shader_Opt opt);
#define mel_gpu_shader_init(shader, dev, ...) mel_gpu_shader_init_opt((shader), (dev), (Mel_Gpu_Shader_Opt){__VA_ARGS__})

typedef struct {
    str8 path;
    Mel_Gpu_Device* dev;
    Mel_Counter* on_finish;
    str8 vertex_entry;
    str8 fragment_entry;
    str8 compute_entry;
    str8 task_entry;
    str8 mesh_entry;
    const Mel_Alloc* alloc;
} Mel_Gpu_Shader_Load_Opt;

void mel_gpu_shader_load_opt(Mel_Gpu_Shader* shader, Mel_Gpu_Shader_Load_Opt opt);
#define mel_gpu_shader_load(shader, ...) mel_gpu_shader_load_opt((shader), (Mel_Gpu_Shader_Load_Opt){__VA_ARGS__})

void mel_gpu_shader_shutdown(Mel_Gpu_Shader* shader, Mel_Gpu_Device* dev);

bool mel_slang_init(void);
void mel_slang_shutdown(void);

extern Mel_Event_Channel mel_slang_ready;

#ifdef __cplusplus
}
#endif
