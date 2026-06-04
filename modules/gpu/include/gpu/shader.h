#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>

#include <slang/compile.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

MEL_GPU_HANDLE(Mel_Gpu_Shader);

typedef enum
{
    MEL_GPU_SHADER_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_SHADER_CREATE_BACKEND_FAILED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_SHADER_CREATE_NO_CODE = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Shader_Create_Status;

typedef enum
{
    MEL_GPU_SHADER_TARGET_SPIRV = 0,
    MEL_GPU_SHADER_TARGET_MSL = 1,
    MEL_GPU_SHADER_TARGET_DXIL = 2,
    MEL_GPU_SHADER_TARGET_WGSL = 3,
} Mel_Gpu_Shader_Target;

typedef struct
{
    Mel_Gpu_Shader_Target target;

    const void* vertex_blob;
    usize       vertex_blob_size;
    const void* fragment_blob;
    usize       fragment_blob_size;

    const void* spirv_vertex;
    usize       spirv_vertex_size;
    const void* spirv_fragment;
    usize       spirv_fragment_size;

    const char* vertex_entry;
    const char* fragment_entry;
    const char* name;
} Mel_Gpu_Shader_Bytecode_Opt;

typedef struct
{
    Mel_Gpu_Shader               value;
    Mel_Gpu_Shader_Create_Status status;
} Mel_Gpu_Shader_Create_Result;

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Bytecode_Opt opt);
#define mel_gpu_shader_create_from_bytecode(dev, ...) mel_gpu_shader_create_from_bytecode_opt((dev), (Mel_Gpu_Shader_Bytecode_Opt){ __VA_ARGS__ })

typedef struct
{
    Mel_Gpu_Shader_Target target;

    const void* compute_blob;
    usize       compute_blob_size;

    const void* spirv;
    usize       spirv_size;

    const char* entry;
    const char* name;
} Mel_Gpu_Shader_Compute_Opt;

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_compute_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Compute_Opt opt);
#define mel_gpu_shader_create_compute_from_bytecode(dev, ...) mel_gpu_shader_create_compute_from_bytecode_opt((dev), (Mel_Gpu_Shader_Compute_Opt){ __VA_ARGS__ })

typedef struct
{
    const char* source;
    const char* vertex_entry;
    const char* fragment_entry;
    const char* compute_entry;
    const char* name;
} Mel_Gpu_Shader_Slang_Opt;

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_from_slang_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Slang_Opt opt);
#define mel_gpu_shader_create_from_slang(dev, ...) mel_gpu_shader_create_from_slang_opt((dev), (Mel_Gpu_Shader_Slang_Opt){ __VA_ARGS__ })

bool mel_gpu_slang_target_for_device(Mel_Gpu_Device* dev, Mel_Slang_Target* out_target);

void mel_gpu_shader_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh);
bool mel_gpu_shader_alive(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh);
