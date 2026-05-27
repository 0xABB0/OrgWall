#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <gpu/gpu.h>

struct Mel_Gpu_Device {
    id<MTLDevice>       mtl;
    id<MTLCommandQueue> queue;
};

struct Mel_Gpu_Buffer {
    id<MTLBuffer> mtl;
    usize         size;
    bool          host_visible;
};

struct Mel_Gpu_Shader {
    id<MTLLibrary>  library;
    id<MTLFunction> vertex_fn;
    id<MTLFunction> fragment_fn;
};

struct Mel_Gpu_Pipeline {
    id<MTLRenderPipelineState> state;
    MTLPrimitiveType           primitive;
    MTLCullMode                cull;
};

struct Mel_Gpu_Command_List {
    Mel_Gpu_Swapchain*          swapchain;
    id<MTLRenderCommandEncoder> encoder;
    MTLPrimitiveType            primitive;
};

struct Mel_Gpu_Swapchain {
    Mel_Gpu_Device* device;
    CAMetalLayer*   layer;
    Mel_Gpu_Format  format;
    i32             width, height;

    id<CAMetalDrawable>  drawable;   // current frame, nil between frames
    id<MTLCommandBuffer> cmd_buffer; // current frame, nil between frames
    Mel_Gpu_Command_List cmd;        // reused wrapper handed to the app
};

static inline MTLPixelFormat mel_gpu__mtl_pixel_format(Mel_Gpu_Format f)
{
    switch (f) {
        case MEL_GPU_FORMAT_BGRA8_UNORM: return MTLPixelFormatBGRA8Unorm;
        case MEL_GPU_FORMAT_RGBA8_UNORM: return MTLPixelFormatRGBA8Unorm;
        default:                         return MTLPixelFormatBGRA8Unorm;
    }
}

static inline MTLVertexFormat mel_gpu__mtl_vertex_format(Mel_Gpu_Format f)
{
    switch (f) {
        case MEL_GPU_FORMAT_RG32_FLOAT:   return MTLVertexFormatFloat2;
        case MEL_GPU_FORMAT_RGB32_FLOAT:  return MTLVertexFormatFloat3;
        case MEL_GPU_FORMAT_RGBA32_FLOAT: return MTLVertexFormatFloat4;
        default:                          return MTLVertexFormatFloat3;
    }
}

static inline MTLPrimitiveType mel_gpu__mtl_primitive(Mel_Gpu_Topology t)
{
    switch (t) {
        case MEL_GPU_TOPOLOGY_TRIANGLE_LIST:  return MTLPrimitiveTypeTriangle;
        case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP: return MTLPrimitiveTypeTriangleStrip;
        case MEL_GPU_TOPOLOGY_LINE_LIST:      return MTLPrimitiveTypeLine;
        case MEL_GPU_TOPOLOGY_POINT_LIST:     return MTLPrimitiveTypePoint;
        default:                              return MTLPrimitiveTypeTriangle;
    }
}
