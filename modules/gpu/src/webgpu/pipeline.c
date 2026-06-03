#include "wgpu_backend.h"

#include <log/log.h>

#include <stdlib.h>

static WGPUVertexFormat mel_gpu__vertex_format(Mel_Gpu_Format f)
{
    switch (f)
    {
    case MEL_GPU_FORMAT_RG32_FLOAT:
        return WGPUVertexFormat_Float32x2;
    case MEL_GPU_FORMAT_RGB32_FLOAT:
        return WGPUVertexFormat_Float32x3;
    case MEL_GPU_FORMAT_RGBA32_FLOAT:
        return WGPUVertexFormat_Float32x4;
    default:
        return WGPUVertexFormat_Float32x4;
    }
}

static WGPUPrimitiveTopology mel_gpu__topology(Mel_Gpu_Topology t)
{
    switch (t)
    {
    case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP:
        return WGPUPrimitiveTopology_TriangleStrip;
    case MEL_GPU_TOPOLOGY_LINE_LIST:
        return WGPUPrimitiveTopology_LineList;
    case MEL_GPU_TOPOLOGY_POINT_LIST:
        return WGPUPrimitiveTopology_PointList;
    case MEL_GPU_TOPOLOGY_TRIANGLE_LIST:
    default:
        return WGPUPrimitiveTopology_TriangleList;
    }
}

static WGPUCullMode mel_gpu__cull(Mel_Gpu_Cull c)
{
    switch (c)
    {
    case MEL_GPU_CULL_FRONT:
        return WGPUCullMode_Front;
    case MEL_GPU_CULL_BACK:
        return WGPUCullMode_Back;
    case MEL_GPU_CULL_NONE:
    default:
        return WGPUCullMode_None;
    }
}

static WGPUBlendFactor mel_gpu__blend_factor(Mel_Gpu_Blend_Factor f)
{
    switch (f)
    {
    case MEL_GPU_BLEND_ZERO:
        return WGPUBlendFactor_Zero;
    case MEL_GPU_BLEND_ONE:
        return WGPUBlendFactor_One;
    case MEL_GPU_BLEND_SRC_COLOR:
        return WGPUBlendFactor_Src;
    case MEL_GPU_BLEND_ONE_MINUS_SRC_COLOR:
        return WGPUBlendFactor_OneMinusSrc;
    case MEL_GPU_BLEND_DST_COLOR:
        return WGPUBlendFactor_Dst;
    case MEL_GPU_BLEND_ONE_MINUS_DST_COLOR:
        return WGPUBlendFactor_OneMinusDst;
    case MEL_GPU_BLEND_SRC_ALPHA:
        return WGPUBlendFactor_SrcAlpha;
    case MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA:
        return WGPUBlendFactor_OneMinusSrcAlpha;
    case MEL_GPU_BLEND_DST_ALPHA:
        return WGPUBlendFactor_DstAlpha;
    case MEL_GPU_BLEND_ONE_MINUS_DST_ALPHA:
        return WGPUBlendFactor_OneMinusDstAlpha;
    case MEL_GPU_BLEND_CONSTANT_COLOR:
        return WGPUBlendFactor_Constant;
    case MEL_GPU_BLEND_ONE_MINUS_CONSTANT_COLOR:
        return WGPUBlendFactor_OneMinusConstant;
    case MEL_GPU_BLEND_SRC_ALPHA_SATURATE:
        return WGPUBlendFactor_SrcAlphaSaturated;
    default:
        return WGPUBlendFactor_One;
    }
}

static WGPUBlendOperation mel_gpu__blend_op(Mel_Gpu_Blend_Op op)
{
    switch (op)
    {
    case MEL_GPU_BLEND_OP_SUBTRACT:
        return WGPUBlendOperation_Subtract;
    case MEL_GPU_BLEND_OP_REVERSE_SUBTRACT:
        return WGPUBlendOperation_ReverseSubtract;
    case MEL_GPU_BLEND_OP_MIN:
        return WGPUBlendOperation_Min;
    case MEL_GPU_BLEND_OP_MAX:
        return WGPUBlendOperation_Max;
    case MEL_GPU_BLEND_OP_ADD:
    default:
        return WGPUBlendOperation_Add;
    }
}

static WGPUCompareFunction mel_gpu__compare(Mel_Gpu_Compare_Op c)
{
    switch (c)
    {
    case MEL_GPU_COMPARE_NEVER:
        return WGPUCompareFunction_Never;
    case MEL_GPU_COMPARE_LESS:
        return WGPUCompareFunction_Less;
    case MEL_GPU_COMPARE_EQUAL:
        return WGPUCompareFunction_Equal;
    case MEL_GPU_COMPARE_LESS_EQUAL:
        return WGPUCompareFunction_LessEqual;
    case MEL_GPU_COMPARE_GREATER:
        return WGPUCompareFunction_Greater;
    case MEL_GPU_COMPARE_NOT_EQUAL:
        return WGPUCompareFunction_NotEqual;
    case MEL_GPU_COMPARE_GREATER_EQUAL:
        return WGPUCompareFunction_GreaterEqual;
    case MEL_GPU_COMPARE_ALWAYS:
    default:
        return WGPUCompareFunction_Always;
    }
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    Mel_Gpu_Pipeline_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };

    Mel_Gpu_Shader_Obj sh;
    if (!mel_gpu__table_get_copy(dev, &dev->shaders, opt.shader.slot, &sh) || !sh.vertex || !sh.fragment)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        mel_log_error("gpu", "pipeline_create: graphics shader handle missing vertex/fragment modules ('%s')", opt.name ? opt.name : "(unnamed)");
        return res;
    }

    if (opt.push_constant_size)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        mel_log_error("gpu", "pipeline_create: push constants (%u bytes) are not in WebGPU core (MissingFeature); pipeline '%s' refused", opt.push_constant_size, opt.name ? opt.name : "(unnamed)");
        return res;
    }
    if (opt.bindless || opt.set_layout_count || opt.static_sampler_count)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        mel_log_error("gpu", "pipeline_create: explicit bind-group layouts / bindless / static samplers are not implemented this round (MissingFeature); pipeline '%s' refused", opt.name ? opt.name : "(unnamed)");
        return res;
    }

    WGPUVertexAttribute*  attrs = NULL;
    WGPUVertexBufferLayout vbl = { 0 };
    if (opt.vertex_layout_count)
    {
        attrs = calloc(opt.vertex_layout_count, sizeof *attrs);
        for (u32 i = 0; i < opt.vertex_layout_count; i++)
        {
            attrs[i].format = mel_gpu__vertex_format(opt.vertex_layout[i].format);
            attrs[i].offset = opt.vertex_layout[i].offset;
            attrs[i].shaderLocation = opt.vertex_layout[i].location;
        }
        vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.arrayStride = opt.vertex_stride;
        vbl.attributeCount = opt.vertex_layout_count;
        vbl.attributes = attrs;
    }

    WGPUVertexState vstate = {
        .module = sh.vertex,
        .entryPoint = mel_gpu__sv(sh.vertex_entry),
        .bufferCount = opt.vertex_layout_count ? 1 : 0,
        .buffers = opt.vertex_layout_count ? &vbl : NULL,
    };

    u32                  ntargets = opt.color_target_count ? opt.color_target_count : 1;
    WGPUColorTargetState targets[8] = { 0 };
    WGPUBlendState       blends[8] = { 0 };
    if (ntargets > 8)
        ntargets = 8;
    for (u32 i = 0; i < ntargets; i++)
    {
        Mel_Gpu_Format fmt = opt.color_target_count ? opt.color_targets[i].format : opt.color_format;
        targets[i].format = mel_gpu__wgpu_format(fmt);
        targets[i].writeMask = WGPUColorWriteMask_All;
        Mel_Gpu_Blend b = opt.color_target_count ? opt.color_targets[i].blend : (Mel_Gpu_Blend){ .write_mask = MEL_GPU_COLOR_WRITE_ALL };
        if (b.write_mask)
            targets[i].writeMask = (WGPUColorWriteMask)b.write_mask;
        if (b.enable)
        {
            blends[i].color = (WGPUBlendComponent){ .operation = mel_gpu__blend_op(b.color_op), .srcFactor = mel_gpu__blend_factor(b.src_color), .dstFactor = mel_gpu__blend_factor(b.dst_color) };
            blends[i].alpha = (WGPUBlendComponent){ .operation = mel_gpu__blend_op(b.alpha_op), .srcFactor = mel_gpu__blend_factor(b.src_alpha), .dstFactor = mel_gpu__blend_factor(b.dst_alpha) };
            targets[i].blend = &blends[i];
        }
    }

    WGPUFragmentState fstate = {
        .module = sh.fragment,
        .entryPoint = mel_gpu__sv(sh.fragment_entry),
        .targetCount = ntargets,
        .targets = targets,
    };

    WGPUDepthStencilState ds = { 0 };
    bool                  has_depth = opt.depth_format != MEL_GPU_FORMAT_UNDEFINED && opt.depth_stencil;
    if (has_depth)
    {
        ds.format = mel_gpu__wgpu_format(opt.depth_format);
        ds.depthWriteEnabled = opt.depth_stencil->depth_write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        ds.depthCompare = opt.depth_stencil->depth_test ? mel_gpu__compare(opt.depth_stencil->depth_compare) : WGPUCompareFunction_Always;
        ds.stencilFront.compare = WGPUCompareFunction_Always;
        ds.stencilBack.compare = WGPUCompareFunction_Always;
    }

    WGPURenderPipelineDescriptor desc = {
        .label = mel_gpu__sv(opt.name),
        .layout = NULL,
        .vertex = vstate,
        .primitive = {
            .topology = mel_gpu__topology(opt.topology),
            .frontFace = opt.front_face == MEL_GPU_FRONT_FACE_CW ? WGPUFrontFace_CW : WGPUFrontFace_CCW,
            .cullMode = mel_gpu__cull(opt.cull),
        },
        .depthStencil = has_depth ? &ds : NULL,
        .multisample = { .count = opt.samples ? opt.samples : 1, .mask = 0xFFFFFFFFu, .alphaToCoverageEnabled = opt.alpha_to_coverage },
        .fragment = &fstate,
    };

    WGPURenderPipeline wp = wgpuDeviceCreateRenderPipeline(dev->wgpu, &desc);
    free(attrs);
    if (!wp)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
        mel_log_error("gpu", "pipeline_create: wgpuDeviceCreateRenderPipeline returned null for '%s'", opt.name ? opt.name : "(unnamed)");
        return res;
    }

    Mel_Gpu_Pipeline_Obj o = { .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED, .name = opt.name }, .render = wp };
    Mel_SlotMap_Handle   h = mel_gpu__table_insert(dev, &dev->pipelines, &o);
    res.value = (Mel_Gpu_Pipeline){ h };
    return res;
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_compute_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Opt opt)
{
    (void)dev;
    mel_log_error("gpu", "pipeline_compute_create: compute pipelines need bind-group layouts (not implemented this round, MissingFeature); pipeline '%s' refused", opt.name ? opt.name : "(unnamed)");
    return (Mel_Gpu_Pipeline_Create_Result){ .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE };
}

void mel_gpu_pipeline_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe)
{
    Mel_Gpu_Pipeline_Obj* o = mel_gpu__table_get(dev, &dev->pipelines, pipe.slot);
    if (!o)
        return;
    if (o->render)
        wgpuRenderPipelineRelease(o->render);
    if (o->compute)
        wgpuComputePipelineRelease(o->compute);
    mel_gpu__table_remove(dev, &dev->pipelines, pipe.slot);
}

bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe) { return mel_gpu__table_alive(dev, &dev->pipelines, pipe.slot); }
