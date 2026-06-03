#include "mtl_backend.h"

#include <log/log.h>

#include <string.h>

static char* mel_gpu__mtl_strdup(const Mel_Alloc* a, const char* s)
{
    if (!s)
        s = "main";
    usize n = strlen(s) + 1;
    char* d = mel_alloc(a, n);
    memcpy(d, s, n);
    return d;
}

static id<MTLLibrary> mel_gpu__mtl_library(Mel_Gpu_Device* dev, const void* blob, usize size, const char* dbg_name)
{
    NSString* src = [[NSString alloc] initWithBytes:blob length:size encoding:NSUTF8StringEncoding];
    if (!src)
    {
        mel_log_error("gpu", "shader '%s': MSL blob is not valid UTF-8", dbg_name);
        return nil;
    }
    NSError*       err = nil;
    id<MTLLibrary> lib = [dev->mtl newLibraryWithSource:src options:nil error:&err];
    if (!lib)
    {
        mel_log_error("gpu", "shader '%s': newLibraryWithSource failed: %s", dbg_name, err ? err.localizedDescription.UTF8String : "(no error)");
        return nil;
    }
    return lib;
}

static id<MTLFunction> mel_gpu__mtl_function(id<MTLLibrary> lib, const char* entry, const char* dbg_name, const char* stage)
{
    NSString*       name = [NSString stringWithUTF8String:entry ? entry : "main"];
    id<MTLFunction> fn = [lib newFunctionWithName:name];
    if (!fn)
        mel_log_error("gpu", "shader '%s': %s entry point '%s' not found in MSL library", dbg_name, stage, entry ? entry : "main");
    return fn;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Bytecode_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };
    const char*                  dbg_name = opt.name ? opt.name : "(unnamed)";

    if (opt.target != MEL_GPU_SHADER_TARGET_MSL)
    {
        mel_log_error("gpu", "shader '%s': metal accepts only MSL bytecode (target=%d requested)", dbg_name, (int)opt.target);
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        return res;
    }

    const void* vblob = opt.vertex_blob;
    usize       vsize = opt.vertex_blob_size;
    const void* fblob = opt.fragment_blob;
    usize       fsize = opt.fragment_blob_size;
    if (!dev || !vblob || !vsize || !fblob || !fsize)
    {
        mel_log_error("gpu", "shader '%s': missing MSL vertex or fragment blob", dbg_name);
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        return res;
    }

    id<MTLLibrary> vlib = mel_gpu__mtl_library(dev, vblob, vsize, dbg_name);
    id<MTLLibrary> flib = vblob == fblob ? vlib : mel_gpu__mtl_library(dev, fblob, fsize, dbg_name);
    if (!vlib || !flib)
    {
        res.status = MEL_GPU_SHADER_CREATE_VK_FAILED;
        return res;
    }

    id<MTLFunction> vfn = mel_gpu__mtl_function(vlib, opt.vertex_entry, dbg_name, "vertex");
    id<MTLFunction> ffn = mel_gpu__mtl_function(flib, opt.fragment_entry, dbg_name, "fragment");
    if (!vfn || !ffn)
    {
        res.status = MEL_GPU_SHADER_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Shader_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.library = (__bridge_retained void*)vlib;
    obj.vs = (__bridge_retained void*)vfn;
    obj.fs = (__bridge_retained void*)ffn;
    obj.cs = NULL;
    obj.vs_entry = mel_gpu__mtl_strdup(dev->alloc, opt.vertex_entry);
    obj.fs_entry = mel_gpu__mtl_strdup(dev->alloc, opt.fragment_entry);
    obj.cs_entry = NULL;

    res.value.slot = mel_gpu__table_insert(dev, &dev->shaders, &obj);
    return res;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_compute_from_bytecode_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Compute_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };
    const char*                  dbg_name = opt.name ? opt.name : "(unnamed)";

    if (opt.target != MEL_GPU_SHADER_TARGET_MSL)
    {
        mel_log_error("gpu", "compute shader '%s': metal accepts only MSL bytecode (target=%d requested)", dbg_name, (int)opt.target);
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        return res;
    }

    const void* cblob = opt.compute_blob;
    usize       csize = opt.compute_blob_size;
    if (!dev || !cblob || !csize)
    {
        mel_log_error("gpu", "compute shader '%s': missing MSL compute blob", dbg_name);
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        return res;
    }

    id<MTLLibrary> lib = mel_gpu__mtl_library(dev, cblob, csize, dbg_name);
    if (!lib)
    {
        res.status = MEL_GPU_SHADER_CREATE_VK_FAILED;
        return res;
    }
    id<MTLFunction> cfn = mel_gpu__mtl_function(lib, opt.entry, dbg_name, "compute");
    if (!cfn)
    {
        res.status = MEL_GPU_SHADER_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Shader_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.library = (__bridge_retained void*)lib;
    obj.cs = (__bridge_retained void*)cfn;
    obj.cs_entry = mel_gpu__mtl_strdup(dev->alloc, opt.entry);

    res.value.slot = mel_gpu__table_insert(dev, &dev->shaders, &obj);
    return res;
}

void mel_gpu_shader_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh)
{
    Mel_Gpu_Shader_Obj* o = mel_gpu__table_get(dev, &dev->shaders, sh.slot);
    if (!o)
        return;
    if (o->vs)
    {
        id v = (__bridge_transfer id)o->vs;
        o->vs = NULL;
        (void)v;
    }
    if (o->fs)
    {
        id f = (__bridge_transfer id)o->fs;
        o->fs = NULL;
        (void)f;
    }
    if (o->cs)
    {
        id c = (__bridge_transfer id)o->cs;
        o->cs = NULL;
        (void)c;
    }
    if (o->library)
    {
        id l = (__bridge_transfer id)o->library;
        o->library = NULL;
        (void)l;
    }
    if (o->vs_entry)
        mel_dealloc(dev->alloc, o->vs_entry);
    if (o->fs_entry)
        mel_dealloc(dev->alloc, o->fs_entry);
    if (o->cs_entry)
        mel_dealloc(dev->alloc, o->cs_entry);
    mel_gpu__table_remove(dev, &dev->shaders, sh.slot);
}

bool mel_gpu_shader_alive(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh) { return mel_gpu__table_alive(dev, &dev->shaders, sh.slot); }

static bool mel_gpu__shader_functions(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, id<MTLFunction>* vs, id<MTLFunction>* fs)
{
    Mel_Gpu_Shader_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->shaders, sh.slot, &o) || !o.vs || !o.fs)
        return false;
    *vs = (__bridge id<MTLFunction>)o.vs;
    *fs = (__bridge id<MTLFunction>)o.fs;
    return true;
}

static bool mel_gpu__shader_compute_function(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, id<MTLFunction>* cs)
{
    Mel_Gpu_Shader_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->shaders, sh.slot, &o) || !o.cs)
        return false;
    *cs = (__bridge id<MTLFunction>)o.cs;
    return true;
}

static MTLVertexFormat mel_gpu__mtl_vertex_format(Mel_Gpu_Format fmt)
{
    switch (fmt)
    {
    case MEL_GPU_FORMAT_RG32_FLOAT:
        return MTLVertexFormatFloat2;
    case MEL_GPU_FORMAT_RGB32_FLOAT:
        return MTLVertexFormatFloat3;
    case MEL_GPU_FORMAT_RGBA32_FLOAT:
        return MTLVertexFormatFloat4;
    case MEL_GPU_FORMAT_RGBA8_UNORM:
        return MTLVertexFormatUChar4Normalized;
    default:
        return MTLVertexFormatInvalid;
    }
}

MTLPrimitiveType mel_gpu__topology_to_primitive(Mel_Gpu_Topology t)
{
    switch (t)
    {
    case MEL_GPU_TOPOLOGY_TRIANGLE_LIST:
        return MTLPrimitiveTypeTriangle;
    case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP:
        return MTLPrimitiveTypeTriangleStrip;
    case MEL_GPU_TOPOLOGY_LINE_LIST:
        return MTLPrimitiveTypeLine;
    case MEL_GPU_TOPOLOGY_POINT_LIST:
        return MTLPrimitiveTypePoint;
    }
    return MTLPrimitiveTypeTriangle;
}

static MTLBlendFactor mel_gpu__mtl_blend_factor(Mel_Gpu_Blend_Factor f)
{
    switch (f)
    {
    case MEL_GPU_BLEND_ZERO:
        return MTLBlendFactorZero;
    case MEL_GPU_BLEND_ONE:
        return MTLBlendFactorOne;
    case MEL_GPU_BLEND_SRC_COLOR:
        return MTLBlendFactorSourceColor;
    case MEL_GPU_BLEND_ONE_MINUS_SRC_COLOR:
        return MTLBlendFactorOneMinusSourceColor;
    case MEL_GPU_BLEND_DST_COLOR:
        return MTLBlendFactorDestinationColor;
    case MEL_GPU_BLEND_ONE_MINUS_DST_COLOR:
        return MTLBlendFactorOneMinusDestinationColor;
    case MEL_GPU_BLEND_SRC_ALPHA:
        return MTLBlendFactorSourceAlpha;
    case MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA:
        return MTLBlendFactorOneMinusSourceAlpha;
    case MEL_GPU_BLEND_DST_ALPHA:
        return MTLBlendFactorDestinationAlpha;
    case MEL_GPU_BLEND_ONE_MINUS_DST_ALPHA:
        return MTLBlendFactorOneMinusDestinationAlpha;
    case MEL_GPU_BLEND_CONSTANT_COLOR:
        return MTLBlendFactorBlendColor;
    case MEL_GPU_BLEND_ONE_MINUS_CONSTANT_COLOR:
        return MTLBlendFactorOneMinusBlendColor;
    case MEL_GPU_BLEND_CONSTANT_ALPHA:
        return MTLBlendFactorBlendAlpha;
    case MEL_GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA:
        return MTLBlendFactorOneMinusBlendAlpha;
    case MEL_GPU_BLEND_SRC_ALPHA_SATURATE:
        return MTLBlendFactorSourceAlphaSaturated;
    }
    return MTLBlendFactorZero;
}

static MTLBlendOperation mel_gpu__mtl_blend_op(Mel_Gpu_Blend_Op o)
{
    switch (o)
    {
    case MEL_GPU_BLEND_OP_ADD:
        return MTLBlendOperationAdd;
    case MEL_GPU_BLEND_OP_SUBTRACT:
        return MTLBlendOperationSubtract;
    case MEL_GPU_BLEND_OP_REVERSE_SUBTRACT:
        return MTLBlendOperationReverseSubtract;
    case MEL_GPU_BLEND_OP_MIN:
        return MTLBlendOperationMin;
    case MEL_GPU_BLEND_OP_MAX:
        return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

static MTLColorWriteMask mel_gpu__mtl_write_mask(Mel_Gpu_Color_Write_Mask m)
{
    MTLColorWriteMask out = MTLColorWriteMaskNone;
    if (m & MEL_GPU_COLOR_WRITE_R)
        out |= MTLColorWriteMaskRed;
    if (m & MEL_GPU_COLOR_WRITE_G)
        out |= MTLColorWriteMaskGreen;
    if (m & MEL_GPU_COLOR_WRITE_B)
        out |= MTLColorWriteMaskBlue;
    if (m & MEL_GPU_COLOR_WRITE_A)
        out |= MTLColorWriteMaskAlpha;
    return out;
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    Mel_Gpu_Pipeline_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };
    const char*                    dbg_name = opt.name ? opt.name : "(unnamed)";

    id<MTLFunction> vfn = nil, ffn = nil;
    if (!dev || !mel_gpu__shader_functions(dev, opt.shader, &vfn, &ffn))
    {
        mel_log_error("gpu", "pipeline_create '%s': shader has no vertex/fragment functions", dbg_name);
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    if (opt.bindless)
    {
        mel_log_error("gpu", "pipeline_create '%s': bindless pipelines are unsupported on the Metal backend (bindless tier=none)", dbg_name);
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        return res;
    }
    if (opt.spec_constant_count)
        mel_log_warn("gpu", "pipeline_create '%s': %u spec-constant(s) ignored on the Metal backend (no function-constant lowering this round)", dbg_name, opt.spec_constant_count);
    if (opt.static_sampler_count)
        mel_log_warn("gpu", "pipeline_create '%s': %u static sampler(s) ignored on the Metal backend this round", dbg_name, opt.static_sampler_count);

    MTLRenderPipelineDescriptor* rpd = [[MTLRenderPipelineDescriptor alloc] init];
    rpd.vertexFunction = vfn;
    rpd.fragmentFunction = ffn;

    Mel_Gpu_Color_Target        single = { .format = opt.color_format, .blend = MEL_GPU_BLEND_OPAQUE };
    const Mel_Gpu_Color_Target* targets = opt.color_target_count ? opt.color_targets : (opt.color_format != MEL_GPU_FORMAT_UNDEFINED ? &single : NULL);
    u32                         target_count = opt.color_target_count ? opt.color_target_count : (opt.color_format != MEL_GPU_FORMAT_UNDEFINED ? 1u : 0u);
    for (u32 i = 0; i < target_count; i++)
    {
        MTLPixelFormat pf = mel_gpu__mtl_format(targets[i].format);
        if (pf == MTLPixelFormatInvalid)
        {
            mel_log_error("gpu", "pipeline_create '%s': color target %u format %d has no Metal mapping", dbg_name, i, (int)targets[i].format);
            res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
            return res;
        }
        const Mel_Gpu_Blend* b = &targets[i].blend;
        rpd.colorAttachments[i].pixelFormat = pf;
        rpd.colorAttachments[i].writeMask = mel_gpu__mtl_write_mask(b->write_mask);
        rpd.colorAttachments[i].blendingEnabled = b->enable ? YES : NO;
        rpd.colorAttachments[i].sourceRGBBlendFactor = mel_gpu__mtl_blend_factor(b->src_color);
        rpd.colorAttachments[i].destinationRGBBlendFactor = mel_gpu__mtl_blend_factor(b->dst_color);
        rpd.colorAttachments[i].rgbBlendOperation = mel_gpu__mtl_blend_op(b->color_op);
        rpd.colorAttachments[i].sourceAlphaBlendFactor = mel_gpu__mtl_blend_factor(b->src_alpha);
        rpd.colorAttachments[i].destinationAlphaBlendFactor = mel_gpu__mtl_blend_factor(b->dst_alpha);
        rpd.colorAttachments[i].alphaBlendOperation = mel_gpu__mtl_blend_op(b->alpha_op);
    }

    if (opt.depth_format != MEL_GPU_FORMAT_UNDEFINED)
    {
        MTLPixelFormat df = mel_gpu__mtl_format(opt.depth_format);
        if (df == MTLPixelFormatInvalid)
        {
            mel_log_error("gpu", "pipeline_create '%s': depth format %d has no Metal mapping", dbg_name, (int)opt.depth_format);
            res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
            return res;
        }
        rpd.depthAttachmentPixelFormat = df;
        if (opt.depth_format == MEL_GPU_FORMAT_D24_UNORM_S8_UINT)
            rpd.stencilAttachmentPixelFormat = df;
    }

    u32 req_samples = opt.samples ? opt.samples : 1;
    rpd.rasterSampleCount = req_samples;

    if (opt.vertex_layout_count && opt.vertex_stride)
    {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        bool                 ok = true;
        for (u32 i = 0; i < opt.vertex_layout_count; i++)
        {
            MTLVertexFormat vf = mel_gpu__mtl_vertex_format(opt.vertex_layout[i].format);
            if (vf == MTLVertexFormatInvalid)
            {
                mel_log_error("gpu", "pipeline_create '%s': vertex element %u format %d has no Metal vertex format", dbg_name, i, (int)opt.vertex_layout[i].format);
                ok = false;
                break;
            }
            vd.attributes[opt.vertex_layout[i].location].format = vf;
            vd.attributes[opt.vertex_layout[i].location].offset = opt.vertex_layout[i].offset;
            vd.attributes[opt.vertex_layout[i].location].bufferIndex = MEL_GPU_METAL_VERTEX_BUFFER_INDEX;
        }
        if (!ok)
        {
            res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
            return res;
        }
        vd.layouts[MEL_GPU_METAL_VERTEX_BUFFER_INDEX].stride = opt.vertex_stride;
        vd.layouts[MEL_GPU_METAL_VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;
        rpd.vertexDescriptor = vd;
    }

    NSError*                   err = nil;
    id<MTLRenderPipelineState> state = [dev->mtl newRenderPipelineStateWithDescriptor:rpd error:&err];
    if (!state)
    {
        mel_log_error("gpu", "pipeline_create '%s': newRenderPipelineState failed: %s", dbg_name, err ? err.localizedDescription.UTF8String : "(no error)");
        res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Pipeline_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.state = (__bridge_retained void*)state;
    obj.topology = opt.topology;
    obj.compute = false;
    res.value.slot = mel_gpu__table_insert(dev, &dev->pipelines, &obj);
    return res;
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_compute_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Opt opt)
{
    Mel_Gpu_Pipeline_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };
    const char*                    dbg_name = opt.name ? opt.name : "(unnamed)";

    id<MTLFunction> cfn = nil;
    if (!dev || !mel_gpu__shader_compute_function(dev, opt.shader, &cfn))
    {
        mel_log_error("gpu", "pipeline_compute_create '%s': shader has no compute function", dbg_name);
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }
    if (opt.bindless)
    {
        mel_log_error("gpu", "pipeline_compute_create '%s': bindless pipelines are unsupported on the Metal backend", dbg_name);
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        return res;
    }
    if (opt.spec_constant_count)
        mel_log_warn("gpu", "pipeline_compute_create '%s': %u spec-constant(s) ignored on the Metal backend this round", dbg_name, opt.spec_constant_count);

    NSError*                    err = nil;
    id<MTLComputePipelineState> state = [dev->mtl newComputePipelineStateWithFunction:cfn error:&err];
    if (!state)
    {
        mel_log_error("gpu", "pipeline_compute_create '%s': newComputePipelineState failed: %s", dbg_name, err ? err.localizedDescription.UTF8String : "(no error)");
        res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Pipeline_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.state = (__bridge_retained void*)state;
    obj.compute = true;
    obj.threadgroup = MTLSizeMake(state.threadExecutionWidth, 1, 1);
    res.value.slot = mel_gpu__table_insert(dev, &dev->pipelines, &obj);
    return res;
}

void mel_gpu_pipeline_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe)
{
    Mel_Gpu_Pipeline_Obj* o = mel_gpu__table_get(dev, &dev->pipelines, pipe.slot);
    if (!o)
        return;
    if (o->state)
    {
        id s = (__bridge_transfer id)o->state;
        o->state = NULL;
        (void)s;
    }
    mel_gpu__table_remove(dev, &dev->pipelines, pipe.slot);
}

bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe) { return mel_gpu__table_alive(dev, &dev->pipelines, pipe.slot); }

bool mel_gpu__pipeline_get(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, Mel_Gpu_Pipeline_Obj* out) { return mel_gpu__table_get_copy(dev, &dev->pipelines, pipe.slot, out); }
