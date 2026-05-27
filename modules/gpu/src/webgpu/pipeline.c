#include "webgpu_backend.h"

#define MEL_GPU_MAX_VERTEX_ATTRS 16

static WGPUCullMode mel_gpu__wgpu_cull(Mel_Gpu_Cull c)
{
    switch (c) {
        case MEL_GPU_CULL_FRONT: return WGPUCullMode_Front;
        case MEL_GPU_CULL_BACK:  return WGPUCullMode_Back;
        default:                 return WGPUCullMode_None;
    }
}

Mel_Gpu_Pipeline* mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    if (!dev || !opt.shader) return NULL;

    u32 attr_count = opt.vertex_layout_count;
    if (attr_count > MEL_GPU_MAX_VERTEX_ATTRS) attr_count = MEL_GPU_MAX_VERTEX_ATTRS;

    WGPUVertexAttribute attrs[MEL_GPU_MAX_VERTEX_ATTRS] = {0};
    for (u32 i = 0; i < attr_count; i++) {
        const Mel_Gpu_Vertex_Element* e = &opt.vertex_layout[i];
        attrs[i].format         = mel_gpu__wgpu_vertex_format(e->format);
        attrs[i].offset         = e->offset;
        attrs[i].shaderLocation = e->location;
    }

    WGPUVertexBufferLayout vb = {
        .stepMode       = WGPUVertexStepMode_Vertex,
        .arrayStride    = opt.vertex_stride,
        .attributeCount = attr_count,
        .attributes     = attrs,
    };

    WGPUColorTargetState color = {
        .format    = mel_gpu__wgpu_color_format(opt.color_format),
        .writeMask = WGPUColorWriteMask_All,
    };

    WGPUFragmentState frag = {
        .module      = opt.shader->module,
        .entryPoint  = mel_gpu__sv(opt.shader->fragment_entry),
        .targetCount = 1,
        .targets     = &color,
    };

    WGPURenderPipelineDescriptor d = {
        .layout = NULL, // auto pipeline layout (no bind groups in this pipeline)
        .vertex = {
            .module      = opt.shader->module,
            .entryPoint  = mel_gpu__sv(opt.shader->vertex_entry),
            .bufferCount = opt.vertex_stride > 0 ? 1 : 0,
            .buffers     = opt.vertex_stride > 0 ? &vb : NULL,
        },
        .primitive = {
            .topology         = mel_gpu__wgpu_topology(opt.topology),
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace        = WGPUFrontFace_CCW,
            .cullMode         = mel_gpu__wgpu_cull(opt.cull),
        },
        .multisample = { .count = 1, .mask = 0xFFFFFFFFu, .alphaToCoverageEnabled = false },
        .fragment    = &frag,
    };

    WGPURenderPipeline pipe = wgpuDeviceCreateRenderPipeline(dev->device, &d);
    if (!pipe) return NULL;

    Mel_Gpu_Pipeline* p = calloc(1, sizeof *p);
    if (!p) { wgpuRenderPipelineRelease(pipe); return NULL; }
    p->pipe = pipe;
    return p;
}

void mel_gpu_pipeline_destroy(Mel_Gpu_Pipeline* pipe)
{
    if (!pipe) return;
    if (pipe->pipe) wgpuRenderPipelineRelease(pipe->pipe);
    free(pipe);
}
