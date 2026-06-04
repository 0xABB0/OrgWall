#pragma once

#include <stddef.h>

#include <gpu/caps.h>
#include <gpu/device.h>
#include <gpu/shader.h>

#include <log/log.h>

typedef struct
{
    const char* name;

    const void* spirv_vertex;
    usize       spirv_vertex_size;
    const void* spirv_fragment;
    usize       spirv_fragment_size;

    const void* msl_vertex;
    usize       msl_vertex_size;
    const void* msl_fragment;
    usize       msl_fragment_size;

    const void* wgsl_vertex;
    usize       wgsl_vertex_size;
    const void* wgsl_fragment;
    usize       wgsl_fragment_size;

    const void* dxil_vertex;
    usize       dxil_vertex_size;
    const void* dxil_fragment;
    usize       dxil_fragment_size;

    const char* vertex_entry;
    const char* fragment_entry;
} Mel_Bundle_Graphics;

static inline Mel_Gpu_Shader_Create_Result mel_bundle_select_graphics(Mel_Gpu_Device* dev, const Mel_Bundle_Graphics* bundle)
{
    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);

    if (caps->shader.bytecode_passthrough.msl && bundle->msl_vertex)
        return mel_gpu_shader_create_from_bytecode(dev,
                                                   .target = MEL_GPU_SHADER_TARGET_MSL,
                                                   .vertex_blob = bundle->msl_vertex,
                                                   .vertex_blob_size = bundle->msl_vertex_size,
                                                   .fragment_blob = bundle->msl_fragment,
                                                   .fragment_blob_size = bundle->msl_fragment_size,
                                                   .vertex_entry = bundle->vertex_entry,
                                                   .fragment_entry = bundle->fragment_entry,
                                                   .name = bundle->name);

    if (caps->shader.bytecode_passthrough.wgsl && bundle->wgsl_vertex)
        return mel_gpu_shader_create_from_bytecode(dev,
                                                   .target = MEL_GPU_SHADER_TARGET_WGSL,
                                                   .vertex_blob = bundle->wgsl_vertex,
                                                   .vertex_blob_size = bundle->wgsl_vertex_size,
                                                   .fragment_blob = bundle->wgsl_fragment,
                                                   .fragment_blob_size = bundle->wgsl_fragment_size,
                                                   .vertex_entry = bundle->vertex_entry,
                                                   .fragment_entry = bundle->fragment_entry,
                                                   .name = bundle->name);

    if (caps->shader.bytecode_passthrough.dxil && bundle->dxil_vertex)
        return mel_gpu_shader_create_from_bytecode(dev,
                                                   .target = MEL_GPU_SHADER_TARGET_DXIL,
                                                   .vertex_blob = bundle->dxil_vertex,
                                                   .vertex_blob_size = bundle->dxil_vertex_size,
                                                   .fragment_blob = bundle->dxil_fragment,
                                                   .fragment_blob_size = bundle->dxil_fragment_size,
                                                   .vertex_entry = bundle->vertex_entry,
                                                   .fragment_entry = bundle->fragment_entry,
                                                   .name = bundle->name);

    if (caps->shader.bytecode_passthrough.spirv && bundle->spirv_vertex)
        return mel_gpu_shader_create_from_bytecode(dev,
                                                   .target = MEL_GPU_SHADER_TARGET_SPIRV,
                                                   .vertex_blob = bundle->spirv_vertex,
                                                   .vertex_blob_size = bundle->spirv_vertex_size,
                                                   .fragment_blob = bundle->spirv_fragment,
                                                   .fragment_blob_size = bundle->spirv_fragment_size,
                                                   .vertex_entry = bundle->vertex_entry,
                                                   .fragment_entry = bundle->fragment_entry,
                                                   .name = bundle->name);

    mel_log_error("gpu",
                  "bundle '%s': no shader form the device accepts — device passthrough caps {msl=%d wgsl=%d dxil=%d spirv=%d}; bundle has {msl=%d wgsl=%d dxil=%d spirv=%d}",
                  bundle->name,
                  caps->shader.bytecode_passthrough.msl,
                  caps->shader.bytecode_passthrough.wgsl,
                  caps->shader.bytecode_passthrough.dxil,
                  caps->shader.bytecode_passthrough.spirv,
                  bundle->msl_vertex != NULL,
                  bundle->wgsl_vertex != NULL,
                  bundle->dxil_vertex != NULL,
                  bundle->spirv_vertex != NULL);

    return (Mel_Gpu_Shader_Create_Result){ .status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED };
}
