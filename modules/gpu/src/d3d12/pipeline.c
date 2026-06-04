#include "d3d_backend.h"

#include <gpu/pipeline.h>
#include <log/log.h>

static D3D12_BLEND mel_gpu__blend(Mel_Gpu_Blend_Factor f)
{
    switch (f)
    {
    case MEL_GPU_BLEND_ZERO:
        return D3D12_BLEND_ZERO;
    case MEL_GPU_BLEND_ONE:
        return D3D12_BLEND_ONE;
    case MEL_GPU_BLEND_SRC_COLOR:
        return D3D12_BLEND_SRC_COLOR;
    case MEL_GPU_BLEND_ONE_MINUS_SRC_COLOR:
        return D3D12_BLEND_INV_SRC_COLOR;
    case MEL_GPU_BLEND_DST_COLOR:
        return D3D12_BLEND_DEST_COLOR;
    case MEL_GPU_BLEND_ONE_MINUS_DST_COLOR:
        return D3D12_BLEND_INV_DEST_COLOR;
    case MEL_GPU_BLEND_SRC_ALPHA:
        return D3D12_BLEND_SRC_ALPHA;
    case MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA:
        return D3D12_BLEND_INV_SRC_ALPHA;
    case MEL_GPU_BLEND_DST_ALPHA:
        return D3D12_BLEND_DEST_ALPHA;
    case MEL_GPU_BLEND_ONE_MINUS_DST_ALPHA:
        return D3D12_BLEND_INV_DEST_ALPHA;
    case MEL_GPU_BLEND_CONSTANT_COLOR:
    case MEL_GPU_BLEND_CONSTANT_ALPHA:
        return D3D12_BLEND_BLEND_FACTOR;
    case MEL_GPU_BLEND_ONE_MINUS_CONSTANT_COLOR:
    case MEL_GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA:
        return D3D12_BLEND_INV_BLEND_FACTOR;
    case MEL_GPU_BLEND_SRC_ALPHA_SATURATE:
        return D3D12_BLEND_SRC_ALPHA_SAT;
    default:
        return D3D12_BLEND_ONE;
    }
}

static D3D12_BLEND_OP mel_gpu__blend_op(Mel_Gpu_Blend_Op o)
{
    switch (o)
    {
    case MEL_GPU_BLEND_OP_SUBTRACT:
        return D3D12_BLEND_OP_SUBTRACT;
    case MEL_GPU_BLEND_OP_REVERSE_SUBTRACT:
        return D3D12_BLEND_OP_REV_SUBTRACT;
    case MEL_GPU_BLEND_OP_MIN:
        return D3D12_BLEND_OP_MIN;
    case MEL_GPU_BLEND_OP_MAX:
        return D3D12_BLEND_OP_MAX;
    case MEL_GPU_BLEND_OP_ADD:
    default:
        return D3D12_BLEND_OP_ADD;
    }
}

static D3D12_COMPARISON_FUNC mel_gpu__compare_func(Mel_Gpu_Compare_Op c)
{
    switch (c)
    {
    case MEL_GPU_COMPARE_NEVER:
        return D3D12_COMPARISON_FUNC_NEVER;
    case MEL_GPU_COMPARE_LESS:
        return D3D12_COMPARISON_FUNC_LESS;
    case MEL_GPU_COMPARE_EQUAL:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case MEL_GPU_COMPARE_LESS_EQUAL:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case MEL_GPU_COMPARE_GREATER:
        return D3D12_COMPARISON_FUNC_GREATER;
    case MEL_GPU_COMPARE_NOT_EQUAL:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case MEL_GPU_COMPARE_GREATER_EQUAL:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case MEL_GPU_COMPARE_ALWAYS:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    case MEL_GPU_COMPARE_NONE:
    default:
        return D3D12_COMPARISON_FUNC_LESS;
    }
}

static D3D12_STENCIL_OP mel_gpu__stencil(Mel_Gpu_Stencil_Op s)
{
    switch (s)
    {
    case MEL_GPU_STENCIL_ZERO:
        return D3D12_STENCIL_OP_ZERO;
    case MEL_GPU_STENCIL_REPLACE:
        return D3D12_STENCIL_OP_REPLACE;
    case MEL_GPU_STENCIL_INCREMENT_CLAMP:
        return D3D12_STENCIL_OP_INCR_SAT;
    case MEL_GPU_STENCIL_DECREMENT_CLAMP:
        return D3D12_STENCIL_OP_DECR_SAT;
    case MEL_GPU_STENCIL_INVERT:
        return D3D12_STENCIL_OP_INVERT;
    case MEL_GPU_STENCIL_INCREMENT_WRAP:
        return D3D12_STENCIL_OP_INCR;
    case MEL_GPU_STENCIL_DECREMENT_WRAP:
        return D3D12_STENCIL_OP_DECR;
    case MEL_GPU_STENCIL_KEEP:
    default:
        return D3D12_STENCIL_OP_KEEP;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE mel_gpu__topo_type(Mel_Gpu_Topology t)
{
    switch (t)
    {
    case MEL_GPU_TOPOLOGY_LINE_LIST:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case MEL_GPU_TOPOLOGY_POINT_LIST:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case MEL_GPU_TOPOLOGY_TRIANGLE_LIST:
    case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP:
    default:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY mel_gpu__topo_ia(Mel_Gpu_Topology t)
{
    switch (t)
    {
    case MEL_GPU_TOPOLOGY_TRIANGLE_STRIP:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case MEL_GPU_TOPOLOGY_LINE_LIST:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case MEL_GPU_TOPOLOGY_POINT_LIST:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case MEL_GPU_TOPOLOGY_TRIANGLE_LIST:
    default:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

static D3D12_STATIC_BORDER_COLOR mel_gpu__static_border(const float c[4])
{
    if (c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f && c[3] == 0.0f)
        return D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    if (c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f)
        return D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    return D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
}

static u32 mel_gpu__set_binding_count(const Mel_Gpu_Bind_Group_Layout_Obj* lo, bool want_sampler)
{
    u32 n = 0;
    for (u32 i = 0; i < lo->entry_count; i++)
        if (mel_gpu__descriptor_is_sampler(lo->entries[i].kind) == want_sampler)
            n++;
    return n;
}

static bool mel_gpu__build_root_sig(Mel_Gpu_Device* dev, bool bindless, bool is_compute, u32 pc_size, const Mel_Gpu_Bind_Group_Layout* set_layouts, u32 set_layout_count, Mel_Gpu_Set_Param** out_set_params, u32* out_set_param_count, const Mel_Gpu_Static_Sampler* static_samplers, u32 static_sampler_count, ID3D12RootSignature** out)
{
    if (out_set_params)
        *out_set_params = NULL;
    if (out_set_param_count)
        *out_set_param_count = 0;

    u32 classic_sets = bindless ? 0u : set_layout_count;
    u32 max_params = 1 + (bindless ? 2u : 0u) + classic_sets * 2u;
    u32 max_ranges = (bindless ? 5u : 0u) + (classic_sets ? 1u : 0u);
    for (u32 s = 0; s < classic_sets; s++)
    {
        Mel_Gpu_Bind_Group_Layout_Obj* lo = NULL;
        if (mel_gpu__bind_group_layout_get(dev, set_layouts[s], &lo))
            max_ranges += lo->entry_count;
    }

    D3D12_ROOT_PARAMETER1*  params = mel_alloc(dev->alloc, sizeof(D3D12_ROOT_PARAMETER1) * (max_params ? max_params : 1));
    D3D12_DESCRIPTOR_RANGE1* ranges = mel_alloc(dev->alloc, sizeof(D3D12_DESCRIPTOR_RANGE1) * (max_ranges ? max_ranges : 1));
    Mel_Gpu_Set_Param*      set_params = classic_sets ? mel_alloc(dev->alloc, sizeof(Mel_Gpu_Set_Param) * classic_sets) : NULL;
    u32                     nparams = 0;
    u32                     nranges = 0;

    if (pc_size > 0)
    {
        params[nparams] = (D3D12_ROOT_PARAMETER1){ .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL };
        params[nparams].Constants.ShaderRegister = 0;
        params[nparams].Constants.RegisterSpace = 0;
        params[nparams].Constants.Num32BitValues = (pc_size + 3) / 4;
        nparams++;
    }

    const D3D12_DESCRIPTOR_RANGE_FLAGS vol = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    if (bindless)
    {
        u32 base = nranges;
        ranges[nranges++] = (D3D12_DESCRIPTOR_RANGE1){ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = dev->cap_sampled_image, .BaseShaderRegister = 0, .RegisterSpace = 0, .Flags = vol, .OffsetInDescriptorsFromTableStart = dev->base_sampled_image };
        ranges[nranges++] = (D3D12_DESCRIPTOR_RANGE1){ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = dev->cap_storage_buffer, .BaseShaderRegister = 0, .RegisterSpace = 0, .Flags = vol, .OffsetInDescriptorsFromTableStart = dev->base_storage_buffer };
        ranges[nranges++] = (D3D12_DESCRIPTOR_RANGE1){ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV, .NumDescriptors = dev->cap_uniform_buffer, .BaseShaderRegister = 1, .RegisterSpace = 0, .Flags = vol, .OffsetInDescriptorsFromTableStart = dev->base_uniform_buffer };
        ranges[nranges++] = (D3D12_DESCRIPTOR_RANGE1){ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = dev->cap_storage_image, .BaseShaderRegister = 0, .RegisterSpace = 1, .Flags = vol, .OffsetInDescriptorsFromTableStart = dev->base_storage_image };
        params[nparams] = (D3D12_ROOT_PARAMETER1){ .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL };
        params[nparams].DescriptorTable.NumDescriptorRanges = 4;
        params[nparams].DescriptorTable.pDescriptorRanges = &ranges[base];
        nparams++;
        u32 smp_base = nranges;
        ranges[nranges++] = (D3D12_DESCRIPTOR_RANGE1){ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, .NumDescriptors = dev->smp_cap, .BaseShaderRegister = 0, .RegisterSpace = 0, .Flags = vol, .OffsetInDescriptorsFromTableStart = 0 };
        params[nparams] = (D3D12_ROOT_PARAMETER1){ .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL };
        params[nparams].DescriptorTable.NumDescriptorRanges = 1;
        params[nparams].DescriptorTable.pDescriptorRanges = &ranges[smp_base];
        nparams++;
    }

    for (u32 s = 0; s < classic_sets; s++)
    {
        set_params[s] = (Mel_Gpu_Set_Param){ 0 };
        Mel_Gpu_Bind_Group_Layout_Obj* lo = NULL;
        if (!mel_gpu__bind_group_layout_get(dev, set_layouts[s], &lo))
        {
            mel_log_error("gpu", "pipeline_create: set_layouts[%u] is not a live bind-group layout", s);
            mel_dealloc(dev->alloc, params);
            mel_dealloc(dev->alloc, ranges);
            if (set_params)
                mel_dealloc(dev->alloc, set_params);
            return false;
        }

        u32 res_in_set = mel_gpu__set_binding_count(lo, false);
        u32 smp_in_set = mel_gpu__set_binding_count(lo, true);
        if (res_in_set)
        {
            u32 base = nranges;
            u32 off = 0;
            for (u32 i = 0; i < lo->entry_count; i++)
            {
                if (mel_gpu__descriptor_is_sampler(lo->entries[i].kind))
                    continue;
                u32 cnt = lo->entries[i].count ? lo->entries[i].count : 1u;
                ranges[nranges++] = (D3D12_DESCRIPTOR_RANGE1){ .RangeType = mel_gpu__range_type(lo->entries[i].kind), .NumDescriptors = cnt, .BaseShaderRegister = lo->entries[i].binding, .RegisterSpace = s, .Flags = vol, .OffsetInDescriptorsFromTableStart = off };
                off += cnt;
            }
            params[nparams] = (D3D12_ROOT_PARAMETER1){ .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL };
            params[nparams].DescriptorTable.NumDescriptorRanges = nranges - base;
            params[nparams].DescriptorTable.pDescriptorRanges = &ranges[base];
            set_params[s].has_resource = true;
            set_params[s].resource_param = nparams;
            nparams++;
        }
        if (smp_in_set)
        {
            u32 base = nranges;
            u32 off = 0;
            for (u32 i = 0; i < lo->entry_count; i++)
            {
                if (!mel_gpu__descriptor_is_sampler(lo->entries[i].kind))
                    continue;
                u32 cnt = lo->entries[i].count ? lo->entries[i].count : 1u;
                ranges[nranges++] = (D3D12_DESCRIPTOR_RANGE1){ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, .NumDescriptors = cnt, .BaseShaderRegister = lo->entries[i].binding, .RegisterSpace = s, .Flags = vol, .OffsetInDescriptorsFromTableStart = off };
                off += cnt;
            }
            params[nparams] = (D3D12_ROOT_PARAMETER1){ .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL };
            params[nparams].DescriptorTable.NumDescriptorRanges = nranges - base;
            params[nparams].DescriptorTable.pDescriptorRanges = &ranges[base];
            set_params[s].has_sampler = true;
            set_params[s].sampler_param = nparams;
            nparams++;
        }
    }

    D3D12_STATIC_SAMPLER_DESC* statics = NULL;
    if (static_sampler_count > 0)
    {
        statics = mel_alloc(dev->alloc, sizeof(D3D12_STATIC_SAMPLER_DESC) * static_sampler_count);
        for (u32 i = 0; i < static_sampler_count; i++)
        {
            D3D12_SAMPLER_DESC sd = { 0 };
            if (!mel_gpu__sampler_desc(dev, static_samplers[i].sampler, &sd))
                mel_log_warn("gpu", "pipeline_create: static sampler %u handle invalid", i);
            statics[i] = (D3D12_STATIC_SAMPLER_DESC){
                .Filter = sd.Filter,
                .AddressU = sd.AddressU,
                .AddressV = sd.AddressV,
                .AddressW = sd.AddressW,
                .MipLODBias = sd.MipLODBias,
                .MaxAnisotropy = sd.MaxAnisotropy,
                .ComparisonFunc = sd.ComparisonFunc,
                .BorderColor = mel_gpu__static_border(sd.BorderColor),
                .MinLOD = sd.MinLOD,
                .MaxLOD = sd.MaxLOD,
                .ShaderRegister = static_samplers[i].binding,
                .RegisterSpace = 0,
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
            };
        }
    }

    D3D12_ROOT_SIGNATURE_FLAGS flags = is_compute ? D3D12_ROOT_SIGNATURE_FLAG_NONE : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC vdesc = { .Version = D3D_ROOT_SIGNATURE_VERSION_1_1 };
    vdesc.Desc_1_1.NumParameters = nparams;
    vdesc.Desc_1_1.pParameters = nparams ? params : NULL;
    vdesc.Desc_1_1.NumStaticSamplers = static_sampler_count;
    vdesc.Desc_1_1.pStaticSamplers = statics;
    vdesc.Desc_1_1.Flags = flags;

    ID3DBlob* blob = NULL;
    ID3DBlob* err = NULL;
    HRESULT   hr = D3D12SerializeVersionedRootSignature(&vdesc, &blob, &err);
    if (FAILED(hr))
    {
        mel_log_error("gpu", "D3D12SerializeVersionedRootSignature failed: 0x%08lx %s", (unsigned long)hr, err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "");
        if (err)
            ID3D10Blob_Release(err);
        if (statics)
            mel_dealloc(dev->alloc, statics);
        mel_dealloc(dev->alloc, params);
        mel_dealloc(dev->alloc, ranges);
        if (set_params)
            mel_dealloc(dev->alloc, set_params);
        return false;
    }
    hr = ID3D12Device_CreateRootSignature(dev->d3d, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void**)out);
    ID3D10Blob_Release(blob);
    if (err)
        ID3D10Blob_Release(err);
    if (statics)
        mel_dealloc(dev->alloc, statics);
    mel_dealloc(dev->alloc, params);
    mel_dealloc(dev->alloc, ranges);
    if (FAILED(hr))
    {
        mel_log_error("gpu", "CreateRootSignature failed: 0x%08lx", (unsigned long)hr);
        if (set_params)
            mel_dealloc(dev->alloc, set_params);
        return false;
    }
    if (out_set_params)
        *out_set_params = set_params;
    else if (set_params)
        mel_dealloc(dev->alloc, set_params);
    if (out_set_param_count)
        *out_set_param_count = classic_sets;
    return true;
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt)
{
    Mel_Gpu_Pipeline_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };

    Mel_Gpu_Shader_Obj* sh = NULL;
    if (!dev || !mel_gpu__shader_get(dev, opt.shader, &sh) || sh->is_compute)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        mel_log_error("gpu", "pipeline_create: invalid or non-graphics shader handle");
        return res;
    }

    bool bindless = opt.bindless;
    u32  pc_size = opt.push_constant_size;

    if (bindless && !dev->bindless_enabled)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        mel_log_error("gpu", "pipeline_create: bindless pipeline but the device has no bindless heap (request descriptor_indexing at device-create)");
        return res;
    }
    if (!bindless && opt.set_layout_count > 0 && !dev->classic_res_heap)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        mel_log_error("gpu", "pipeline_create: set_layouts require the classic descriptor heap, which failed to initialize");
        return res;
    }

    if (opt.spec_constant_count > 0)
        mel_log_warn("gpu", "pipeline_create: specialization constants have no DXIL analog; %u ignored (recompile with -D defines on D3D12)", opt.spec_constant_count);

    ID3D12RootSignature* root_sig = NULL;
    Mel_Gpu_Set_Param*   set_params = NULL;
    u32                  set_param_count = 0;
    if (!mel_gpu__build_root_sig(dev, bindless, false, pc_size, opt.set_layouts, opt.set_layout_count, &set_params, &set_param_count, opt.static_samplers, opt.static_sampler_count, &root_sig))
    {
        res.status = MEL_GPU_PIPELINE_CREATE_BACKEND_FAILED;
        return res;
    }

    u32                       input_count = 0;
    u32                       vertex_stride = 0;
    u32*                      slot_strides = NULL;
    u32                       slot_stride_count = 0;
    D3D12_INPUT_ELEMENT_DESC* elems = NULL;
    if (opt.vertex_layout_count > 0)
    {
        input_count = opt.vertex_layout_count;
        vertex_stride = opt.vertex_stride;

        u32 max_slot = 0;
        for (u32 i = 0; i < input_count; i++)
            if (opt.vertex_layout[i].buffer_slot > max_slot)
                max_slot = opt.vertex_layout[i].buffer_slot;
        for (u32 b = 0; b < opt.vertex_buffer_count; b++)
            if (opt.vertex_buffers[b].slot > max_slot)
                max_slot = opt.vertex_buffers[b].slot;
        slot_stride_count = max_slot + 1;
        slot_strides = mel_alloc(dev->alloc, sizeof(u32) * slot_stride_count);
        for (u32 s = 0; s < slot_stride_count; s++)
            slot_strides[s] = 0;
        if (opt.vertex_buffer_count)
            for (u32 b = 0; b < opt.vertex_buffer_count; b++)
                slot_strides[opt.vertex_buffers[b].slot] = opt.vertex_buffers[b].stride;
        else
            slot_strides[0] = opt.vertex_stride;

        elems = mel_alloc(dev->alloc, sizeof(D3D12_INPUT_ELEMENT_DESC) * input_count);
        for (u32 i = 0; i < input_count; i++)
        {
            const char* semantic = "TEXCOORD";
            u32         semantic_index = opt.vertex_layout[i].location;
            for (u32 r = 0; r < sh->input_count; r++)
                if (sh->inputs[r].input_register == opt.vertex_layout[i].location)
                {
                    semantic = sh->inputs[r].semantic;
                    semantic_index = sh->inputs[r].semantic_index;
                    break;
                }
            u32  slot = opt.vertex_layout[i].buffer_slot;
            bool per_instance = false;
            if (opt.vertex_buffer_count)
            {
                bool found = false;
                for (u32 b = 0; b < opt.vertex_buffer_count; b++)
                    if (opt.vertex_buffers[b].slot == slot)
                    {
                        per_instance = opt.vertex_buffers[b].per_instance;
                        found = true;
                        break;
                    }
                if (!found)
                {
                    mel_log_error("gpu", "pipeline_create '%s': vertex element %u references buffer_slot %u with no matching vertex_buffers entry", opt.name ? opt.name : "(unnamed)", i, slot);
                    mel_dealloc(dev->alloc, elems);
                    mel_dealloc(dev->alloc, slot_strides);
                    if (set_params)
                        mel_dealloc(dev->alloc, set_params);
                    ID3D12RootSignature_Release(root_sig);
                    res.status = MEL_GPU_PIPELINE_CREATE_BACKEND_FAILED;
                    return res;
                }
            }
            elems[i] = (D3D12_INPUT_ELEMENT_DESC){ .SemanticName = semantic, .SemanticIndex = semantic_index, .Format = mel_gpu__dxgi_format(opt.vertex_layout[i].format), .InputSlot = slot, .AlignedByteOffset = opt.vertex_layout[i].offset, .InputSlotClass = per_instance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = per_instance ? 1u : 0u };
        }
    }
    else if (sh->input_count > 0)
    {
        input_count = sh->input_count;
        vertex_stride = sh->vertex_stride;
        slot_stride_count = 1;
        slot_strides = mel_alloc(dev->alloc, sizeof(u32));
        slot_strides[0] = sh->vertex_stride;
        elems = mel_alloc(dev->alloc, sizeof(D3D12_INPUT_ELEMENT_DESC) * input_count);
        for (u32 i = 0; i < input_count; i++)
            elems[i] = (D3D12_INPUT_ELEMENT_DESC){ .SemanticName = sh->inputs[i].semantic, .SemanticIndex = sh->inputs[i].semantic_index, .Format = mel_gpu__dxgi_format(sh->inputs[i].format), .InputSlot = 0, .AlignedByteOffset = sh->inputs[i].offset, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0 };
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = { 0 };
    pso.pRootSignature = root_sig;
    pso.VS = (D3D12_SHADER_BYTECODE){ .pShaderBytecode = sh->vs, .BytecodeLength = sh->vs_size };
    pso.PS = (D3D12_SHADER_BYTECODE){ .pShaderBytecode = sh->fs, .BytecodeLength = sh->fs_size };
    pso.SampleMask = 0xFFFFFFFFu;

    pso.BlendState.AlphaToCoverageEnable = opt.alpha_to_coverage;
    pso.BlendState.IndependentBlendEnable = opt.color_target_count > 1;
    u32 rt_count = 0;
    if (opt.color_target_count > 0)
    {
        rt_count = opt.color_target_count > 8 ? 8 : opt.color_target_count;
        for (u32 i = 0; i < rt_count; i++)
        {
            Mel_Gpu_Blend b = opt.color_targets[i].blend;
            pso.BlendState.RenderTarget[i] = (D3D12_RENDER_TARGET_BLEND_DESC){
                .BlendEnable = b.enable,
                .LogicOpEnable = FALSE,
                .SrcBlend = mel_gpu__blend(b.src_color),
                .DestBlend = mel_gpu__blend(b.dst_color),
                .BlendOp = mel_gpu__blend_op(b.color_op),
                .SrcBlendAlpha = mel_gpu__blend(b.src_alpha),
                .DestBlendAlpha = mel_gpu__blend(b.dst_alpha),
                .BlendOpAlpha = mel_gpu__blend_op(b.alpha_op),
                .LogicOp = D3D12_LOGIC_OP_NOOP,
                .RenderTargetWriteMask = b.write_mask ? b.write_mask : D3D12_COLOR_WRITE_ENABLE_ALL,
            };
            pso.RTVFormats[i] = mel_gpu__dxgi_format(opt.color_targets[i].format);
        }
    }
    else if (opt.color_format != MEL_GPU_FORMAT_UNDEFINED)
    {
        rt_count = 1;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.RTVFormats[0] = mel_gpu__dxgi_format(opt.color_format);
    }
    pso.NumRenderTargets = rt_count;

    pso.RasterizerState = (D3D12_RASTERIZER_DESC){
        .FillMode = opt.fill == MEL_GPU_FILL_WIREFRAME ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID,
        .CullMode = opt.cull == MEL_GPU_CULL_FRONT ? D3D12_CULL_MODE_FRONT : (opt.cull == MEL_GPU_CULL_BACK ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE),
        .FrontCounterClockwise = opt.front_face == MEL_GPU_FRONT_FACE_CW ? FALSE : TRUE,
        .DepthBias = opt.depth_bias ? (INT)opt.depth_bias_constant : 0,
        .DepthBiasClamp = opt.depth_bias ? opt.depth_bias_clamp : 0.0f,
        .SlopeScaledDepthBias = opt.depth_bias ? opt.depth_bias_slope : 0.0f,
        .DepthClipEnable = TRUE,
        .MultisampleEnable = opt.samples > 1,
        .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
    };
    if (opt.fill == MEL_GPU_FILL_POINT)
        mel_log_warn("gpu", "pipeline_create: point fill has no D3D12 mode; using solid");

    bool has_depth = opt.depth_format != MEL_GPU_FORMAT_UNDEFINED;
    if (opt.depth_stencil)
    {
        const Mel_Gpu_Depth_Stencil* ds = opt.depth_stencil;
        pso.DepthStencilState.DepthEnable = ds->depth_test;
        pso.DepthStencilState.DepthWriteMask = ds->depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc = mel_gpu__compare_func(ds->depth_compare == MEL_GPU_COMPARE_NONE ? MEL_GPU_COMPARE_LESS : ds->depth_compare);
        pso.DepthStencilState.StencilEnable = ds->stencil_test;
        pso.DepthStencilState.StencilReadMask = (UINT8)ds->front.compare_mask;
        pso.DepthStencilState.StencilWriteMask = (UINT8)ds->front.write_mask;
        pso.DepthStencilState.FrontFace = (D3D12_DEPTH_STENCILOP_DESC){ .StencilFailOp = mel_gpu__stencil(ds->front.fail), .StencilDepthFailOp = mel_gpu__stencil(ds->front.depth_fail), .StencilPassOp = mel_gpu__stencil(ds->front.pass), .StencilFunc = mel_gpu__compare_func(ds->front.compare) };
        pso.DepthStencilState.BackFace = (D3D12_DEPTH_STENCILOP_DESC){ .StencilFailOp = mel_gpu__stencil(ds->back.fail), .StencilDepthFailOp = mel_gpu__stencil(ds->back.depth_fail), .StencilPassOp = mel_gpu__stencil(ds->back.pass), .StencilFunc = mel_gpu__compare_func(ds->back.compare) };
    }
    else if (has_depth)
    {
        pso.DepthStencilState.DepthEnable = TRUE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    }
    pso.DSVFormat = has_depth ? mel_gpu__dxgi_format(opt.depth_format) : DXGI_FORMAT_UNKNOWN;

    pso.InputLayout = (D3D12_INPUT_LAYOUT_DESC){ .pInputElementDescs = elems, .NumElements = input_count };
    pso.PrimitiveTopologyType = mel_gpu__topo_type(opt.topology);
    pso.SampleDesc.Count = opt.samples > 1 ? opt.samples : 1;
    pso.SampleDesc.Quality = 0;

    ID3D12PipelineState* pipe = NULL;
    HRESULT              hr = ID3D12Device_CreateGraphicsPipelineState(dev->d3d, &pso, &IID_ID3D12PipelineState, (void**)&pipe);
    if (elems)
        mel_dealloc(dev->alloc, elems);
    if (FAILED(hr) || !pipe)
    {
        mel_log_error("gpu", "CreateGraphicsPipelineState failed: 0x%08lx", (unsigned long)hr);
        ID3D12RootSignature_Release(root_sig);
        if (set_params)
            mel_dealloc(dev->alloc, set_params);
        if (slot_strides)
            mel_dealloc(dev->alloc, slot_strides);
        res.status = MEL_GPU_PIPELINE_CREATE_BACKEND_FAILED;
        return res;
    }

    Mel_Gpu_Pipeline_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.root_sig = root_sig;
    obj.pso = pipe;
    obj.bindless = bindless;
    obj.is_compute = false;
    obj.topology = mel_gpu__topo_ia(opt.topology);
    obj.push_constant_size = pc_size;
    obj.vertex_stride = vertex_stride;
    obj.slot_strides = slot_strides;
    obj.slot_stride_count = slot_stride_count;
    obj.set_params = set_params;
    obj.set_param_count = set_param_count;
    if (bindless)
    {
        obj.srv_table_param = pc_size > 0 ? 1 : 0;
        obj.smp_table_param = obj.srv_table_param + 1;
    }
    if (opt.static_sampler_count > 0)
    {
        obj.static_samplers = mel_alloc(dev->alloc, sizeof(Mel_Gpu_Sampler) * opt.static_sampler_count);
        obj.static_sampler_count = opt.static_sampler_count;
        for (u32 i = 0; i < opt.static_sampler_count; i++)
        {
            obj.static_samplers[i] = opt.static_samplers[i].sampler;
            mel_gpu__sampler_retain(dev, opt.static_samplers[i].sampler);
        }
    }

    res.value.slot = mel_gpu__table_insert(dev, &dev->pipelines, &obj);
    return res;
}

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_compute_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Opt opt)
{
    Mel_Gpu_Pipeline_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_PIPELINE_CREATE_OK };

    Mel_Gpu_Shader_Obj* sh = NULL;
    if (!dev || !mel_gpu__shader_get(dev, opt.shader, &sh) || !sh->is_compute)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_NO_SHADER;
        mel_log_error("gpu", "pipeline_compute_create: invalid or non-compute shader handle");
        return res;
    }

    bool bindless = opt.bindless;
    u32  pc_size = opt.push_constant_size;

    if (bindless && !dev->bindless_enabled)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        mel_log_error("gpu", "pipeline_compute_create: bindless pipeline but the device has no bindless heap");
        return res;
    }
    if (!bindless && opt.set_layout_count > 0 && !dev->classic_res_heap)
    {
        res.status = MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE;
        mel_log_error("gpu", "pipeline_compute_create: set_layouts require the classic descriptor heap, which failed to initialize");
        return res;
    }
    if (opt.spec_constant_count > 0)
        mel_log_warn("gpu", "pipeline_compute_create: specialization constants have no DXIL analog; %u ignored", opt.spec_constant_count);

    ID3D12RootSignature* root_sig = NULL;
    Mel_Gpu_Set_Param*   set_params = NULL;
    u32                  set_param_count = 0;
    if (!mel_gpu__build_root_sig(dev, bindless, true, pc_size, opt.set_layouts, opt.set_layout_count, &set_params, &set_param_count, NULL, 0, &root_sig))
    {
        res.status = MEL_GPU_PIPELINE_CREATE_BACKEND_FAILED;
        return res;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso = { 0 };
    pso.pRootSignature = root_sig;
    pso.CS = (D3D12_SHADER_BYTECODE){ .pShaderBytecode = sh->cs, .BytecodeLength = sh->cs_size };

    ID3D12PipelineState* pipe = NULL;
    HRESULT              hr = ID3D12Device_CreateComputePipelineState(dev->d3d, &pso, &IID_ID3D12PipelineState, (void**)&pipe);
    if (FAILED(hr) || !pipe)
    {
        mel_log_error("gpu", "CreateComputePipelineState failed: 0x%08lx", (unsigned long)hr);
        ID3D12RootSignature_Release(root_sig);
        if (set_params)
            mel_dealloc(dev->alloc, set_params);
        res.status = MEL_GPU_PIPELINE_CREATE_BACKEND_FAILED;
        return res;
    }

    Mel_Gpu_Pipeline_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.root_sig = root_sig;
    obj.pso = pipe;
    obj.bindless = bindless;
    obj.is_compute = true;
    obj.push_constant_size = pc_size;
    obj.set_params = set_params;
    obj.set_param_count = set_param_count;
    if (bindless)
    {
        obj.srv_table_param = pc_size > 0 ? 1 : 0;
        obj.smp_table_param = obj.srv_table_param + 1;
    }

    res.value.slot = mel_gpu__table_insert(dev, &dev->pipelines, &obj);
    return res;
}

void mel_gpu_pipeline_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe)
{
    Mel_Gpu_Pipeline_Obj* o = mel_gpu__table_get(dev, &dev->pipelines, pipe.slot);
    if (!o)
        return;
    ID3D12PipelineState*  pso = o->pso;
    ID3D12RootSignature*  rs = o->root_sig;
    Mel_Gpu_Sampler*      statics = o->static_samplers;
    u32                   static_count = o->static_sampler_count;
    Mel_Gpu_Set_Param*    set_params = o->set_params;
    u32*                  slot_strides = o->slot_strides;

    mel_gpu__table_remove(dev, &dev->pipelines, pipe.slot);
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .pso = pso, .root_sig = rs });
    if (set_params)
        mel_dealloc(dev->alloc, set_params);
    if (slot_strides)
        mel_dealloc(dev->alloc, slot_strides);
    if (statics)
    {
        for (u32 i = 0; i < static_count; i++)
            mel_gpu_sampler_destroy(dev, statics[i]);
        mel_dealloc(dev->alloc, statics);
    }
}

bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe) { return mel_gpu__table_get(dev, &dev->pipelines, pipe.slot) != NULL; }
