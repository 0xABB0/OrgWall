#include <gpu/shader.h>
#include <gpu/pipeline.h>
#include <gpu/device.h>
#include <gpu/caps.h>

#include <slang/compile.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <log/log.h>

#include <stddef.h>

bool mel_gpu_slang_target_for_device(Mel_Gpu_Device* dev, Mel_Slang_Target* out_target)
{
    if (!dev || !out_target)
        return false;

    const Mel_Gpu_Caps*                             caps = mel_gpu_device_caps(dev);
    const Mel_Gpu_Caps_Shader_Bytecode_Passthrough* pt = &caps->shader.bytecode_passthrough;

    if (pt->spirv)
        *out_target = MEL_SLANG_TARGET_SPIRV;
    else if (pt->msl)
        *out_target = MEL_SLANG_TARGET_MSL;
    else if (pt->wgsl)
        *out_target = MEL_SLANG_TARGET_WGSL;
    else if (pt->dxil)
        *out_target = MEL_SLANG_TARGET_DXIL;
    else
    {
        mel_log_error("gpu", "from-slang: device advertises no bytecode-passthrough target {spirv=%d msl=%d wgsl=%d dxil=%d}", pt->spirv, pt->msl, pt->wgsl, pt->dxil);
        return false;
    }
    return true;
}

static Mel_Gpu_Shader_Target mel_gpu__shader_target_of_slang(Mel_Slang_Target t)
{
    switch (t)
    {
    case MEL_SLANG_TARGET_SPIRV:
        return MEL_GPU_SHADER_TARGET_SPIRV;
    case MEL_SLANG_TARGET_MSL:
        return MEL_GPU_SHADER_TARGET_MSL;
    case MEL_SLANG_TARGET_DXIL:
        return MEL_GPU_SHADER_TARGET_DXIL;
    case MEL_SLANG_TARGET_WGSL:
        return MEL_GPU_SHADER_TARGET_WGSL;
    }
    return MEL_GPU_SHADER_TARGET_SPIRV;
}

static const char* mel_gpu__slang_target_name(Mel_Slang_Target t)
{
    switch (t)
    {
    case MEL_SLANG_TARGET_SPIRV:
        return "SPIRV";
    case MEL_SLANG_TARGET_MSL:
        return "MSL";
    case MEL_SLANG_TARGET_DXIL:
        return "DXIL";
    case MEL_SLANG_TARGET_WGSL:
        return "WGSL";
    }
    return "(unknown)";
}

static const char* mel_gpu__downstream_entry(Mel_Slang_Target target, const char* entry) { return target == MEL_SLANG_TARGET_SPIRV ? "main" : entry; }

/* Translate the wrapper's Metal argument-buffer reflection (a DescriptorHandle push-constant
   struct lowered to an inlined argument buffer) into the backend-facing arg-field plan. Shared
   by the compute and graphics from-slang paths; returns NULL when the source authored no
   bindless argument buffer (every non-Metal target, and Metal sources with no DescriptorHandle
   field). Caller deallocs with the same allocator. */
static Mel_Gpu_Bindless_Arg_Field* mel_gpu__slang_arg_fields(const Mel_Alloc* a, const Mel_Slang_Reflection* refl)
{
    if (!refl->metal_arg_buffer || !refl->metal_arg_field_count)
        return NULL;
    Mel_Gpu_Bindless_Arg_Field* arg_fields = mel_alloc_array(a, Mel_Gpu_Bindless_Arg_Field, refl->metal_arg_field_count);
    for (u32 i = 0; i < refl->metal_arg_field_count; ++i)
    {
        const Mel_Slang_Metal_Arg_Field* f = &refl->metal_arg_fields[i];
        arg_fields[i] = (Mel_Gpu_Bindless_Arg_Field){
            .is_uniform = f->is_uniform,
            .host_offset = f->host_offset,
            .arg_index = f->arg_index,
            .size = f->size,
            .resource_kind = (u32)f->kind,
        };
    }
    return arg_fields;
}

static bool mel_gpu__gpu_format_of_slang(Mel_Slang_Vertex_Format f, Mel_Gpu_Format* out)
{
    switch (f)
    {
    case MEL_SLANG_FORMAT_F32X2:
        *out = MEL_GPU_FORMAT_RG32_FLOAT;
        return true;
    case MEL_SLANG_FORMAT_F32X3:
        *out = MEL_GPU_FORMAT_RGB32_FLOAT;
        return true;
    case MEL_SLANG_FORMAT_F32X4:
        *out = MEL_GPU_FORMAT_RGBA32_FLOAT;
        return true;
    default:
        return false;
    }
}

static Mel_Slang_Blob mel_gpu__slang_compile_stage(const char* source, const char* entry, Mel_Slang_Stage stage, Mel_Slang_Target target, const char* dbg, const char* stage_name, Mel_Slang_Reflection* refl)
{
    Mel_Slang_Blob blob = refl ? mel_slang_compile_reflect(source, entry, stage, target, refl) : mel_slang_compile(source, entry, stage, target);
    if (!blob.data)
    {
        mel_log_error("gpu", "from-slang '%s': %s entry '%s' compile to %s failed:\n%s", dbg, stage_name, entry ? entry : "(null)", mel_gpu__slang_target_name(target), blob.diagnostics ? blob.diagnostics : "(no diagnostics)");
        mel_slang_blob_free(&blob);
    }
    return blob;
}

Mel_Gpu_Shader_Create_Result mel_gpu_shader_create_from_slang_opt(Mel_Gpu_Device* dev, Mel_Gpu_Shader_Slang_Opt opt)
{
    Mel_Gpu_Shader_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SHADER_CREATE_OK };
    const char*                  dbg = opt.name ? opt.name : "(unnamed)";

    if (!dev || !opt.source)
    {
        mel_log_error("gpu", "shader_create_from_slang '%s': null device or source", dbg);
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        return res;
    }

    Mel_Slang_Target target;
    if (!mel_gpu_slang_target_for_device(dev, &target))
    {
        res.status = MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED;
        return res;
    }

    if (opt.compute_entry)
    {
        Mel_Slang_Blob cs = mel_gpu__slang_compile_stage(opt.source, opt.compute_entry, MEL_SLANG_STAGE_COMPUTE, target, dbg, "compute", NULL);
        if (!cs.data)
        {
            res.status = MEL_GPU_SHADER_CREATE_BACKEND_FAILED;
            return res;
        }
        res = mel_gpu_shader_create_compute_from_bytecode(dev,
                                                          .target = mel_gpu__shader_target_of_slang(target),
                                                          .compute_blob = cs.data,
                                                          .compute_blob_size = cs.size,
                                                          .entry = mel_gpu__downstream_entry(target, opt.compute_entry),
                                                          .name = opt.name);
        mel_slang_blob_free(&cs);
        return res;
    }

    if (!opt.vertex_entry || !opt.fragment_entry)
    {
        mel_log_error("gpu", "shader_create_from_slang '%s': graphics shader needs both vertex_entry and fragment_entry", dbg);
        res.status = MEL_GPU_SHADER_CREATE_NO_CODE;
        return res;
    }

    Mel_Slang_Blob vs = mel_gpu__slang_compile_stage(opt.source, opt.vertex_entry, MEL_SLANG_STAGE_VERTEX, target, dbg, "vertex", NULL);
    if (!vs.data)
    {
        res.status = MEL_GPU_SHADER_CREATE_BACKEND_FAILED;
        return res;
    }
    Mel_Slang_Blob fs = mel_gpu__slang_compile_stage(opt.source, opt.fragment_entry, MEL_SLANG_STAGE_FRAGMENT, target, dbg, "fragment", NULL);
    if (!fs.data)
    {
        mel_slang_blob_free(&vs);
        res.status = MEL_GPU_SHADER_CREATE_BACKEND_FAILED;
        return res;
    }

    res = mel_gpu_shader_create_from_bytecode(dev,
                                              .target = mel_gpu__shader_target_of_slang(target),
                                              .vertex_blob = vs.data,
                                              .vertex_blob_size = vs.size,
                                              .fragment_blob = fs.data,
                                              .fragment_blob_size = fs.size,
                                              .vertex_entry = mel_gpu__downstream_entry(target, opt.vertex_entry),
                                              .fragment_entry = mel_gpu__downstream_entry(target, opt.fragment_entry),
                                              .name = opt.name);
    mel_slang_blob_free(&vs);
    mel_slang_blob_free(&fs);
    return res;
}

Mel_Gpu_Pipeline_From_Slang_Result mel_gpu_pipeline_create_from_slang_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Slang_Opt opt)
{
    Mel_Gpu_Pipeline_From_Slang_Result res = { .value = { mel_gpu_handle_null() }, .shader = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };
    const char*                        dbg = opt.name ? opt.name : "(unnamed)";

    if (!dev || !opt.source || !opt.vertex_entry || !opt.fragment_entry)
    {
        mel_log_error("gpu", "pipeline_create_from_slang '%s': null device/source or missing vertex/fragment entry", dbg);
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    Mel_Slang_Target target;
    if (!mel_gpu_slang_target_for_device(dev, &target))
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    Mel_Slang_Reflection refl;
    Mel_Slang_Blob       vs = mel_gpu__slang_compile_stage(opt.source, opt.vertex_entry, MEL_SLANG_STAGE_VERTEX, target, dbg, "vertex", &refl);
    if (!vs.data)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }
    Mel_Slang_Blob fs = mel_gpu__slang_compile_stage(opt.source, opt.fragment_entry, MEL_SLANG_STAGE_FRAGMENT, target, dbg, "fragment", NULL);
    if (!fs.data)
    {
        mel_slang_reflection_free(&refl);
        mel_slang_blob_free(&vs);
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    const Mel_Alloc*            a = mel_alloc_heap();
    Mel_Gpu_Bindless_Arg_Field* arg_fields = mel_gpu__slang_arg_fields(a, &refl);
    Mel_Gpu_Vertex_Element*     layout = refl.vertex_attr_count ? mel_alloc_array(a, Mel_Gpu_Vertex_Element, refl.vertex_attr_count) : NULL;
    bool                        layout_ok = true;
    for (u32 i = 0; i < refl.vertex_attr_count; ++i)
    {
        const Mel_Slang_Vertex_Attr* va = &refl.vertex_attrs[i];
        Mel_Gpu_Format               fmt;
        if (!mel_gpu__gpu_format_of_slang(va->format, &fmt))
        {
            mel_log_error("gpu", "pipeline_create_from_slang '%s': vertex attr '%s' (loc %u) has format %d with no Mel_Gpu_Format equivalent", dbg, va->semantic ? va->semantic : "(null)", va->location, (int)va->format);
            layout_ok = false;
            break;
        }
        layout[i] = (Mel_Gpu_Vertex_Element){ .location = va->location, .format = fmt, .offset = va->offset, .buffer_slot = 0 };
    }

    Mel_Gpu_Shader_Create_Result sh = { .status = MEL_GPU_SHADER_CREATE_OK };
    if (layout_ok)
        sh = mel_gpu_shader_create_from_bytecode(dev,
                                                 .target = mel_gpu__shader_target_of_slang(target),
                                                 .vertex_blob = vs.data,
                                                 .vertex_blob_size = vs.size,
                                                 .fragment_blob = fs.data,
                                                 .fragment_blob_size = fs.size,
                                                 .vertex_entry = mel_gpu__downstream_entry(target, opt.vertex_entry),
                                                 .fragment_entry = mel_gpu__downstream_entry(target, opt.fragment_entry),
                                                 .name = opt.name);

    if (!layout_ok || mel_gpu_failed(sh.status))
    {
        if (arg_fields)
            mel_dealloc(a, arg_fields);
        if (layout)
            mel_dealloc(a, layout);
        mel_slang_reflection_free(&refl);
        mel_slang_blob_free(&vs);
        mel_slang_blob_free(&fs);
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev,
                                                                  .shader = sh.value,
                                                                  .topology = opt.topology,
                                                                  .cull = opt.cull,
                                                                  .front_face = opt.front_face,
                                                                  .fill = opt.fill,
                                                                  .color_format = opt.color_format,
                                                                  .color_targets = opt.color_targets,
                                                                  .color_target_count = opt.color_target_count,
                                                                  .blend_constants = { opt.blend_constants[0], opt.blend_constants[1], opt.blend_constants[2], opt.blend_constants[3] },
                                                                  .depth_format = opt.depth_format,
                                                                  .depth_stencil = opt.depth_stencil,
                                                                  .depth_bias = opt.depth_bias,
                                                                  .depth_bias_constant = opt.depth_bias_constant,
                                                                  .depth_bias_clamp = opt.depth_bias_clamp,
                                                                  .depth_bias_slope = opt.depth_bias_slope,
                                                                  .samples = opt.samples,
                                                                  .alpha_to_coverage = opt.alpha_to_coverage,
                                                                  .sample_shading = opt.sample_shading,
                                                                  .min_sample_shading = opt.min_sample_shading,
                                                                  .vertex_layout = layout,
                                                                  .vertex_layout_count = refl.vertex_attr_count,
                                                                  .vertex_stride = refl.vertex_stride,
                                                                  .vertex_buffers = opt.vertex_buffers,
                                                                  .vertex_buffer_count = opt.vertex_buffer_count,
                                                                  .push_constant_size = refl.push_constant_size,
                                                                  .bindless = opt.bindless,
                                                                  .set_layouts = opt.set_layouts,
                                                                  .set_layout_count = opt.set_layout_count,
                                                                  .static_samplers = opt.static_samplers,
                                                                  .static_sampler_count = opt.static_sampler_count,
                                                                  .spec_constants = opt.spec_constants,
                                                                  .spec_constant_count = opt.spec_constant_count,
                                                                  .bindless_arg_fields = arg_fields,
                                                                  .bindless_arg_field_count = refl.metal_arg_buffer ? refl.metal_arg_field_count : 0,
                                                                  .name = opt.name);

    if (mel_gpu_failed(pipe.status))
        mel_gpu_shader_destroy(dev, sh.value);
    else
        res.shader = sh.value;
    res.value = pipe.value;
    res.status = pipe.status;

    if (arg_fields)
        mel_dealloc(a, arg_fields);
    if (layout)
        mel_dealloc(a, layout);
    mel_slang_reflection_free(&refl);
    mel_slang_blob_free(&vs);
    mel_slang_blob_free(&fs);
    return res;
}

Mel_Gpu_Pipeline_From_Slang_Result mel_gpu_pipeline_compute_create_from_slang_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Slang_Opt opt)
{
    Mel_Gpu_Pipeline_From_Slang_Result res = { .value = { mel_gpu_handle_null() }, .shader = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };
    const char*                        dbg = opt.name ? opt.name : "(unnamed)";

    if (!dev || !opt.source || !opt.compute_entry)
    {
        mel_log_error("gpu", "pipeline_compute_create_from_slang '%s': null device/source or missing compute entry", dbg);
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    Mel_Slang_Target target;
    if (!mel_gpu_slang_target_for_device(dev, &target))
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    Mel_Slang_Reflection refl;
    Mel_Slang_Blob       cs = mel_gpu__slang_compile_stage(opt.source, opt.compute_entry, MEL_SLANG_STAGE_COMPUTE, target, dbg, "compute", &refl);
    if (!cs.data)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev,
                                                                                  .target = mel_gpu__shader_target_of_slang(target),
                                                                                  .compute_blob = cs.data,
                                                                                  .compute_blob_size = cs.size,
                                                                                  .entry = mel_gpu__downstream_entry(target, opt.compute_entry),
                                                                                  .name = opt.name);
    if (mel_gpu_failed(sh.status))
    {
        mel_slang_reflection_free(&refl);
        mel_slang_blob_free(&cs);
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        return res;
    }

    const Mel_Alloc*            a = mel_alloc_heap();
    Mel_Gpu_Bindless_Arg_Field* arg_fields = mel_gpu__slang_arg_fields(a, &refl);

    u32                            pcs = opt.push_constant_size ? opt.push_constant_size : refl.push_constant_size;
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev,
                                                                          .shader = sh.value,
                                                                          .push_constant_size = pcs,
                                                                          .bindless = opt.bindless,
                                                                          .set_layouts = opt.set_layouts,
                                                                          .set_layout_count = opt.set_layout_count,
                                                                          .spec_constants = opt.spec_constants,
                                                                          .spec_constant_count = opt.spec_constant_count,
                                                                          .threadgroup = { refl.workgroup[0], refl.workgroup[1], refl.workgroup[2] },
                                                                          .bindless_arg_fields = arg_fields,
                                                                          .bindless_arg_field_count = refl.metal_arg_buffer ? refl.metal_arg_field_count : 0,
                                                                          .name = opt.name);

    if (mel_gpu_failed(pipe.status))
        mel_gpu_shader_destroy(dev, sh.value);
    else
        res.shader = sh.value;
    res.value = pipe.value;
    res.status = pipe.status;

    if (arg_fields)
        mel_dealloc(a, arg_fields);
    mel_slang_reflection_free(&refl);
    mel_slang_blob_free(&cs);
    return res;
}
