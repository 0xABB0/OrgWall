#include "mtl_backend.h"

#include <log/log.h>

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Bytecode_Opt opt)
{
    (void)dev;
    mel_log_error("gpu", "shader_create_from_bytecode: SPIR-V is not translated to MSL on the Metal backend this round; shader '%s' unavailable", opt.name ? opt.name : "(unnamed)");
    return (Mel_Gpu_Shader_Create_Result){ .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_NO_CODE };
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_compute_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Compute_Opt opt)
{
    (void)dev;
    mel_log_error("gpu", "shader_create_compute_from_bytecode: SPIR-V is not translated to MSL on the Metal backend this round; compute shader '%s' unavailable", opt.name ? opt.name : "(unnamed)");
    return (Mel_Gpu_Shader_Create_Result){ .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_NO_CODE };
}

void mel_gpu_shader_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh)
{
    (void)dev;
    (void)sh;
}

bool mel_gpu_shader_alive(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh)
{
    (void)dev;
    (void)sh;
    return false;
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    (void)dev;
    mel_log_error("gpu", "pipeline_create: no graphics-pipeline lowering on the Metal backend this round (needs a shader path); pipeline '%s' unavailable", opt.name ? opt.name : "(unnamed)");
    return (Mel_Gpu_Pipeline_Create_Result){ .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_NO_SHADER };
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_compute_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Opt opt)
{
    (void)dev;
    mel_log_error("gpu", "pipeline_compute_create: no compute-pipeline lowering on the Metal backend this round; pipeline '%s' unavailable", opt.name ? opt.name : "(unnamed)");
    return (Mel_Gpu_Pipeline_Create_Result){ .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_NO_SHADER };
}

void mel_gpu_pipeline_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe)
{
    (void)dev;
    (void)pipe;
}

bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe)
{
    (void)dev;
    (void)pipe;
    return false;
}
