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

static MTLCompareFunction mel_gpu__mtl_compare(Mel_Gpu_Compare_Op c)
{
    switch (c)
    {
    case MEL_GPU_COMPARE_NONE:
    case MEL_GPU_COMPARE_NEVER:
        return MTLCompareFunctionNever;
    case MEL_GPU_COMPARE_LESS:
        return MTLCompareFunctionLess;
    case MEL_GPU_COMPARE_EQUAL:
        return MTLCompareFunctionEqual;
    case MEL_GPU_COMPARE_LESS_EQUAL:
        return MTLCompareFunctionLessEqual;
    case MEL_GPU_COMPARE_GREATER:
        return MTLCompareFunctionGreater;
    case MEL_GPU_COMPARE_NOT_EQUAL:
        return MTLCompareFunctionNotEqual;
    case MEL_GPU_COMPARE_GREATER_EQUAL:
        return MTLCompareFunctionGreaterEqual;
    case MEL_GPU_COMPARE_ALWAYS:
        return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionNever;
}

static MTLStencilOperation mel_gpu__mtl_stencil_op(Mel_Gpu_Stencil_Op o)
{
    switch (o)
    {
    case MEL_GPU_STENCIL_KEEP:
        return MTLStencilOperationKeep;
    case MEL_GPU_STENCIL_ZERO:
        return MTLStencilOperationZero;
    case MEL_GPU_STENCIL_REPLACE:
        return MTLStencilOperationReplace;
    case MEL_GPU_STENCIL_INCREMENT_CLAMP:
        return MTLStencilOperationIncrementClamp;
    case MEL_GPU_STENCIL_DECREMENT_CLAMP:
        return MTLStencilOperationDecrementClamp;
    case MEL_GPU_STENCIL_INVERT:
        return MTLStencilOperationInvert;
    case MEL_GPU_STENCIL_INCREMENT_WRAP:
        return MTLStencilOperationIncrementWrap;
    case MEL_GPU_STENCIL_DECREMENT_WRAP:
        return MTLStencilOperationDecrementWrap;
    }
    return MTLStencilOperationKeep;
}

static MTLStencilDescriptor* mel_gpu__mtl_stencil_face(Mel_Gpu_Stencil_Face f)
{
    MTLStencilDescriptor* d = [[MTLStencilDescriptor alloc] init];
    d.stencilFailureOperation = mel_gpu__mtl_stencil_op(f.fail);
    d.depthStencilPassOperation = mel_gpu__mtl_stencil_op(f.pass);
    d.depthFailureOperation = mel_gpu__mtl_stencil_op(f.depth_fail);
    d.stencilCompareFunction = mel_gpu__mtl_compare(f.compare);
    d.readMask = f.compare_mask;
    d.writeMask = f.write_mask;
    return d;
}

static MTLCullMode mel_gpu__mtl_cull(Mel_Gpu_Cull c)
{
    switch (c)
    {
    case MEL_GPU_CULL_NONE:
        return MTLCullModeNone;
    case MEL_GPU_CULL_FRONT:
        return MTLCullModeFront;
    case MEL_GPU_CULL_BACK:
        return MTLCullModeBack;
    }
    return MTLCullModeNone;
}

static MTLTriangleFillMode mel_gpu__mtl_fill(Mel_Gpu_Fill fill, const char* dbg_name)
{
    switch (fill)
    {
    case MEL_GPU_FILL_SOLID:
        return MTLTriangleFillModeFill;
    case MEL_GPU_FILL_WIREFRAME:
        return MTLTriangleFillModeLines;
    case MEL_GPU_FILL_POINT:
        mel_log_warn("gpu", "pipeline_create '%s': point fill mode is unsupported on the Metal backend (no MTLTriangleFillMode point); using wireframe", dbg_name);
        return MTLTriangleFillModeLines;
    }
    return MTLTriangleFillModeFill;
}

static id<MTLDepthStencilState> mel_gpu__mtl_depth_stencil_state(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt, const char* dbg_name)
{
    bool has_depth = opt.depth_format != MEL_GPU_FORMAT_UNDEFINED;
    if (!opt.depth_stencil)
    {
        if (!has_depth)
            return nil;
        MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
        dsd.depthCompareFunction = MTLCompareFunctionLess;
        dsd.depthWriteEnabled = YES;
        return [dev->mtl newDepthStencilStateWithDescriptor:dsd];
    }

    const Mel_Gpu_Depth_Stencil* d = opt.depth_stencil;
    if (!has_depth)
        mel_log_warn("gpu", "pipeline_create '%s': depth_stencil supplied with no depth_format; the bound render pass must still provide a depth attachment for it to take effect", dbg_name);
    if (d->depth_bounds_test)
        mel_log_warn("gpu", "pipeline_create '%s': depth-bounds test requested but the Metal backend has no depth-bounds equivalent; ignored", dbg_name);

    MTLCompareFunction depth_cmp;
    if (d->depth_test && d->depth_compare == MEL_GPU_COMPARE_NONE)
    {
        mel_log_warn("gpu", "pipeline_create '%s': depth_test set with compare NONE; using LESS", dbg_name);
        depth_cmp = MTLCompareFunctionLess;
    }
    else if (d->depth_test)
        depth_cmp = mel_gpu__mtl_compare(d->depth_compare);
    else
        depth_cmp = MTLCompareFunctionAlways;

    MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = depth_cmp;
    dsd.depthWriteEnabled = d->depth_write ? YES : NO;
    if (d->stencil_test)
    {
        dsd.frontFaceStencil = mel_gpu__mtl_stencil_face(d->front);
        dsd.backFaceStencil = mel_gpu__mtl_stencil_face(d->back);
    }
    return [dev->mtl newDepthStencilStateWithDescriptor:dsd];
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

    id<MTLDepthStencilState> dss = mel_gpu__mtl_depth_stencil_state(dev, opt, dbg_name);
    if ((opt.depth_stencil || opt.depth_format != MEL_GPU_FORMAT_UNDEFINED) && !dss)
    {
        mel_log_error("gpu", "pipeline_create '%s': newDepthStencilState returned nil", dbg_name);
        res.status = MEL_GPU_PIPELINE_CREATE_VK_FAILED;
        return res;
    }

    Mel_Gpu_Pipeline_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.state = (__bridge_retained void*)state;
    obj.depth_stencil_state = dss ? (__bridge_retained void*)dss : NULL;
    obj.topology = opt.topology;
    obj.compute = false;
    obj.cull_mode = mel_gpu__mtl_cull(opt.cull);
    obj.front_face = opt.front_face == MEL_GPU_FRONT_FACE_CW ? MTLWindingClockwise : MTLWindingCounterClockwise;
    obj.fill_mode = mel_gpu__mtl_fill(opt.fill, dbg_name);
    obj.stencil_test = opt.depth_stencil ? opt.depth_stencil->stencil_test : false;
    obj.stencil_ref_front = opt.depth_stencil ? opt.depth_stencil->front.reference : 0;
    obj.stencil_ref_back = opt.depth_stencil ? opt.depth_stencil->back.reference : 0;
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
    if (o->depth_stencil_state)
    {
        id d = (__bridge_transfer id)o->depth_stencil_state;
        o->depth_stencil_state = NULL;
        (void)d;
    }
    mel_gpu__table_remove(dev, &dev->pipelines, pipe.slot);
}

bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe) { return mel_gpu__table_alive(dev, &dev->pipelines, pipe.slot); }

bool mel_gpu__pipeline_get(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, Mel_Gpu_Pipeline_Obj* out) { return mel_gpu__table_get_copy(dev, &dev->pipelines, pipe.slot, out); }
