#pragma once

#include <string.h>
#include <stdlib.h>

#include <webgpu/webgpu.h>

#include <gpu/gpu.h>

struct Mel_Gpu_Device {
    WGPUInstance instance;
    WGPUDevice   device;
    WGPUQueue    queue;
};

struct Mel_Gpu_Buffer {
    WGPUBuffer buf;
    usize      size;
    bool       host_visible;
};

struct Mel_Gpu_Shader {
    WGPUShaderModule module;
    char*            vertex_entry;
    char*            fragment_entry;
};

struct Mel_Gpu_Pipeline {
    WGPURenderPipeline pipe;
};

struct Mel_Gpu_Command_List {
    Mel_Gpu_Swapchain*     swapchain;
    WGPUCommandEncoder     encoder;
    WGPURenderPassEncoder  pass;
};

struct Mel_Gpu_Swapchain {
    Mel_Gpu_Device*   device;
    WGPUSurface       surface;
    WGPUTextureFormat format;
    Mel_Gpu_Format    mel_format;
    i32               width, height;
    char              selector[96];

    WGPUTexture          cur_texture; // current frame, NULL between frames
    WGPUTextureView      cur_view;    // current frame, NULL between frames
    Mel_Gpu_Command_List cmd;         // reused wrapper handed to the app
};

static inline WGPUStringView mel_gpu__sv(const char* s)
{
    WGPUStringView v = { s, s ? strlen(s) : 0 };
    return v;
}

static inline WGPUTextureFormat mel_gpu__wgpu_color_format(Mel_Gpu_Format f)
{
    switch (f) {
        case MEL_GPU_FORMAT_RGBA8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
        case MEL_GPU_FORMAT_BGRA8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
        default:                         return WGPUTextureFormat_BGRA8Unorm;
    }
}

static inline WGPUVertexFormat mel_gpu__wgpu_vertex_format(Mel_Gpu_Format f)
{
    switch (f) {
        case MEL_GPU_FORMAT_RG32_FLOAT:   return WGPUVertexFormat_Float32x2;
        case MEL_GPU_FORMAT_RGB32_FLOAT:  return WGPUVertexFormat_Float32x3;
        case MEL_GPU_FORMAT_RGBA32_FLOAT: return WGPUVertexFormat_Float32x4;
        default:                          return WGPUVertexFormat_Float32x3;
    }
}

static inline WGPUPrimitiveTopology mel_gpu__wgpu_topology(Mel_Gpu_Topology t)
{
    switch (t) {
        case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP: return WGPUPrimitiveTopology_TriangleStrip;
        case MEL_GPU_TOPOLOGY_LINE_LIST:      return WGPUPrimitiveTopology_LineList;
        case MEL_GPU_TOPOLOGY_POINT_LIST:     return WGPUPrimitiveTopology_PointList;
        default:                              return WGPUPrimitiveTopology_TriangleList;
    }
}
