#include "d3d_backend.h"

#include <gpu/binding.h>
#include <log/log.h>

// ---- U15: standalone command lists (one DIRECT allocator + list each; per-thread TLS pooling is a later
// optimization). Created closed; begin resets, end closes. ----

Mel_Gpu_Command_List* mel_gpu_command_list_create(Mel_Gpu_Queue* q)
{
    if (!q)
        return NULL;
    Mel_Gpu_Device* dev = q->dev;

    ID3D12CommandAllocator*    allocr = NULL;
    ID3D12GraphicsCommandList* list = NULL;
    if (FAILED(ID3D12Device_CreateCommandAllocator(dev->d3d, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&allocr)) || FAILED(ID3D12Device_CreateCommandList(dev->d3d, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocr, NULL, &IID_ID3D12GraphicsCommandList, (void**)&list)))
    {
        if (allocr)
            ID3D12CommandAllocator_Release(allocr);
        mel_log_error("gpu", "command_list_create: allocator/list creation failed");
        return NULL;
    }
    ID3D12GraphicsCommandList_Close(list); // created open; close so begin can Reset cleanly

    Mel_Gpu_Command_List* cmd = mel_alloc_type(dev->alloc, Mel_Gpu_Command_List);
    *cmd = (Mel_Gpu_Command_List){ .dev = dev, .allocator = allocr, .list = list };
    return cmd;
}

void mel_gpu_command_list_begin(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd);
    ID3D12CommandAllocator_Reset(cmd->allocator);
    ID3D12GraphicsCommandList_Reset(cmd->list, cmd->allocator, NULL);
    cmd->state_count = 0; // U17 state tracking is per-recording (gpu-rhi.md §7.3)
    cmd->recording = true;
}

void mel_gpu_command_list_end(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->recording);
    ID3D12GraphicsCommandList_Close(cmd->list);
    cmd->recording = false;
}

void mel_gpu_command_list_destroy(Mel_Gpu_Command_List* cmd)
{
    if (!cmd)
        return;
    if (cmd->list)
        ID3D12GraphicsCommandList_Release(cmd->list);
    if (cmd->allocator)
        ID3D12CommandAllocator_Release(cmd->allocator);
    if (cmd->states)
        mel_dealloc(cmd->dev->alloc, cmd->states);
    mel_dealloc(cmd->dev->alloc, cmd);
}

// U17 state tracking: validate the declared source against the list's last-recorded state for the
// subresource, then record the destination. First touch accepts the declared source (§7.3).
static void mel_gpu__track_state(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, u32 mip, u32 layer, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    for (u32 i = 0; i < cmd->state_count; i++)
    {
        Mel_Gpu_Cmd_State_Entry* e = &cmd->states[i];
        if (e->tex_index == tex.slot.index && e->tex_generation == tex.slot.generation && e->mip == mip && e->layer == layer)
        {
            if (e->state != src)
            {
                mel_log_error("gpu", "cmd_barrier: state mismatch on subresource (mip=%u, layer=%u): tracked=%d, declared src=%d", mip, layer, (int)e->state, (int)src);
                mel_assert(!"cmd_barrier: declared source state does not match the command list's tracked state");
            }
            e->state = dst;
            return;
        }
    }
    if (cmd->state_count == cmd->state_cap)
    {
        u32 cap = cmd->state_cap ? cmd->state_cap * 2 : 16;
        cmd->states = cmd->states ? mel_realloc(cmd->dev->alloc, cmd->states, sizeof(Mel_Gpu_Cmd_State_Entry) * cap) : mel_alloc(cmd->dev->alloc, sizeof(Mel_Gpu_Cmd_State_Entry) * cap);
        cmd->state_cap = cap;
    }
    cmd->states[cmd->state_count++] = (Mel_Gpu_Cmd_State_Entry){ .tex_index = tex.slot.index, .tex_generation = tex.slot.generation, .mip = mip, .layer = layer, .state = dst };
}

D3D12_RESOURCE_STATES mel_gpu__state_to_d3d12(Mel_Gpu_Resource_State state)
{
    switch (state)
    {
    case MEL_GPU_STATE_COMMON:
    case MEL_GPU_STATE_PRESENT:
        return D3D12_RESOURCE_STATE_COMMON; // PRESENT == COMMON == 0
    case MEL_GPU_STATE_VERTEX_BUFFER:
    case MEL_GPU_STATE_CONSTANT_BUFFER:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case MEL_GPU_STATE_INDEX_BUFFER:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case MEL_GPU_STATE_SHADER_RESOURCE:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case MEL_GPU_STATE_UNORDERED_ACCESS:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case MEL_GPU_STATE_RENDER_TARGET:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case MEL_GPU_STATE_DEPTH_WRITE:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case MEL_GPU_STATE_DEPTH_READ:
        return D3D12_RESOURCE_STATE_DEPTH_READ;
    case MEL_GPU_STATE_COPY_SOURCE:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case MEL_GPU_STATE_COPY_DEST:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case MEL_GPU_STATE_RESOLVE_SOURCE:
        return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    case MEL_GPU_STATE_RESOLVE_DEST:
        return D3D12_RESOURCE_STATE_RESOLVE_DEST;
    case MEL_GPU_STATE_INDIRECT_ARGUMENT:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case MEL_GPU_STATE_PREDICATION:
        return D3D12_RESOURCE_STATE_PREDICATION;
    case MEL_GPU_STATE_STREAM_OUTPUT:
        return D3D12_RESOURCE_STATE_STREAM_OUT;
    case MEL_GPU_STATE_SHADING_RATE_SOURCE:
        return D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
    case MEL_GPU_STATE_RAY_TRACING_ACCEL_STRUCT:
    case MEL_GPU_STATE_ACCEL_STRUCT_BUILD_READ:
    case MEL_GPU_STATE_ACCEL_STRUCT_BUILD_WRITE:
        return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    default:
        // States whose D3D12 lowering is a later M2/M3 slice (gpu-rhi.md §7.3).
        mel_log_warn("gpu", "cmd_barrier: state %d not yet lowered; using COMMON", (int)state);
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

void mel_gpu_cmd_texture_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range range, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    Mel_Gpu_Texture_Obj* o = NULL;
    if (!cmd || !mel_gpu__texture_get(cmd->dev, tex, &o))
    {
        mel_assert(!"cmd_texture_barrier: invalid texture handle");
        return;
    }

    D3D12_RESOURCE_STATES before = mel_gpu__state_to_d3d12(src);
    D3D12_RESOURCE_STATES after = mel_gpu__state_to_d3d12(dst);

    u32 mip_n = range.mip_count ? range.mip_count : (o->mip_levels - range.base_mip);
    u32 layer_n = range.layer_count ? range.layer_count : (o->array_layers - range.base_layer);
    for (u32 m = 0; m < mip_n; m++)
        for (u32 l = 0; l < layer_n; l++)
        {
            mel_gpu__track_state(cmd, tex, range.base_mip + m, range.base_layer + l, src, dst);
            if (before == after)
                continue; // D3D12 rejects a transition with StateBefore == StateAfter
            D3D12_RESOURCE_BARRIER b = {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Transition = { .pResource = o->resource, .Subresource = (range.base_mip + m) + (range.base_layer + l) * o->mip_levels, .StateBefore = before, .StateAfter = after },
            };
            ID3D12GraphicsCommandList_ResourceBarrier(cmd->list, 1, &b);
        }
}

void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    ID3D12Resource* res = NULL;
    if (!cmd || !mel_gpu__buffer_resource(cmd->dev, buf, &res))
    {
        mel_assert(!"cmd_buffer_barrier: invalid buffer handle");
        return;
    }
    // D3D12 buffers ride common-state promotion/decay, so a transition barrier would mismatch StateBefore.
    // The only buffer barrier needed here is a UAV barrier flushing a UAV hazard (gpu-rhi.md §7.3 lowering).
    if (src == MEL_GPU_STATE_UNORDERED_ACCESS || dst == MEL_GPU_STATE_UNORDERED_ACCESS)
    {
        D3D12_RESOURCE_BARRIER b = { .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV, .UAV = { .pResource = res } };
        ID3D12GraphicsCommandList_ResourceBarrier(cmd->list, 1, &b);
    }
}

void mel_gpu_cmd_copy_texture_to_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range subresource, Mel_Gpu_Buffer dst)
{
    Mel_Gpu_Texture_Obj* o = NULL;
    ID3D12Resource*      dst_res = NULL;
    if (!cmd || !mel_gpu__texture_get(cmd->dev, tex, &o) || !mel_gpu__buffer_resource(cmd->dev, dst, &dst_res))
    {
        mel_assert(!"cmd_copy_texture_to_buffer: invalid handle");
        return;
    }

    UINT sub = subresource.base_mip + subresource.base_layer * o->mip_levels;
    D3D12_RESOURCE_DESC td;
    ID3D12Resource_GetDesc(o->resource, &td);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = { 0 };
    UINT64                             total = 0;
    ID3D12Device_GetCopyableFootprints(cmd->dev->d3d, &td, sub, 1, 0, &fp, NULL, NULL, &total);

    D3D12_TEXTURE_COPY_LOCATION src_loc = { .pResource = o->resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = sub };
    D3D12_TEXTURE_COPY_LOCATION dst_loc = { .pResource = dst_res, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = fp };
    ID3D12GraphicsCommandList_CopyTextureRegion(cmd->list, &dst_loc, 0, 0, 0, &src_loc, NULL);
}

void mel_gpu_cmd_copy_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer src, Mel_Gpu_Buffer dst, usize bytes)
{
    ID3D12Resource* s = NULL;
    ID3D12Resource* d = NULL;
    if (!cmd || !mel_gpu__buffer_resource(cmd->dev, src, &s) || !mel_gpu__buffer_resource(cmd->dev, dst, &d))
    {
        mel_assert(!"cmd_copy_buffer: invalid buffer handle");
        return;
    }
    // Buffers ride common-state promotion (COPY_SOURCE / COPY_DEST auto-promote from COMMON), so no transition
    // barriers; a preceding UAV barrier (cmd_buffer_barrier) flushes a compute-write hazard before the copy.
    ID3D12GraphicsCommandList_CopyBufferRegion(cmd->list, d, 0, s, 0, (UINT64)bytes);
}

// ---- U16: dynamic rendering (OMSetRenderTargets + clears; no render-pass-object compile step) ----

static D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__alloc_rtv(Mel_Gpu_Device* dev)
{
    mel_mutex_lock(&dev->desc_lock);
    u32 i = dev->rtv_next % dev->rtv_cap;
    dev->rtv_next++;
    mel_mutex_unlock(&dev->desc_lock);
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dev->rtv_heap, &h);
    h.ptr += (SIZE_T)i * dev->rtv_size;
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__alloc_dsv(Mel_Gpu_Device* dev)
{
    mel_mutex_lock(&dev->desc_lock);
    u32 i = dev->dsv_next % dev->dsv_cap;
    dev->dsv_next++;
    mel_mutex_unlock(&dev->desc_lock);
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dev->dsv_heap, &h);
    h.ptr += (SIZE_T)i * dev->dsv_size;
    return h;
}

void mel_gpu_cmd_begin_rendering_opt(Mel_Gpu_Command_List* cmd, Mel_Gpu_Rendering_Opt opt)
{
    mel_assert(cmd);
    Mel_Gpu_Device* dev = cmd->dev;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8];
    u32                         n = opt.color_count <= 8 ? opt.color_count : 8;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Texture_View_Obj* v = NULL;
        Mel_Gpu_Texture_Obj*      t = NULL;
        if (!mel_gpu__texture_view_get(dev, opt.colors[i].view, &v))
            continue;
        Mel_Gpu_Texture tex = { v->texture };
        if (!mel_gpu__texture_get(dev, tex, &t))
            continue;
        rtvs[i] = mel_gpu__alloc_rtv(dev);
        D3D12_RENDER_TARGET_VIEW_DESC rtvd = { .Format = v->format, .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D, .Texture2D = { .MipSlice = v->base_mip, .PlaneSlice = 0 } };
        ID3D12Device_CreateRenderTargetView(dev->d3d, t->resource, &rtvd, rtvs[i]);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = { 0 };
    bool                        has_depth = false;
    if (opt.depth)
    {
        Mel_Gpu_Texture_View_Obj* v = NULL;
        Mel_Gpu_Texture_Obj*      t = NULL;
        if (mel_gpu__texture_view_get(dev, opt.depth->view, &v))
        {
            Mel_Gpu_Texture tex = { v->texture };
            if (mel_gpu__texture_get(dev, tex, &t))
            {
                dsv = mel_gpu__alloc_dsv(dev);
                D3D12_DEPTH_STENCIL_VIEW_DESC dsvd = { .Format = v->format, .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D, .Texture2D = { .MipSlice = v->base_mip } };
                ID3D12Device_CreateDepthStencilView(dev->d3d, t->resource, &dsvd, dsv);
                has_depth = true;
            }
        }
    }

    ID3D12GraphicsCommandList_OMSetRenderTargets(cmd->list, n, n ? rtvs : NULL, FALSE, has_depth ? &dsv : NULL);

    for (u32 i = 0; i < n; i++)
        if (opt.colors[i].load == MEL_GPU_LOAD_CLEAR)
        {
            FLOAT c[4] = { opt.colors[i].clear.r, opt.colors[i].clear.g, opt.colors[i].clear.b, opt.colors[i].clear.a };
            ID3D12GraphicsCommandList_ClearRenderTargetView(cmd->list, rtvs[i], c, 0, NULL);
        }
    if (has_depth && opt.depth->load == MEL_GPU_LOAD_CLEAR)
        ID3D12GraphicsCommandList_ClearDepthStencilView(cmd->list, dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, opt.depth->clear_depth, (UINT8)opt.depth->clear_stencil, 0, NULL);

    D3D12_VIEWPORT vp = { .TopLeftX = 0.0f, .TopLeftY = 0.0f, .Width = (FLOAT)opt.width, .Height = (FLOAT)opt.height, .MinDepth = 0.0f, .MaxDepth = 1.0f };
    D3D12_RECT     scissor = { 0, 0, (LONG)opt.width, (LONG)opt.height };
    ID3D12GraphicsCommandList_RSSetViewports(cmd->list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(cmd->list, 1, &scissor);
}

void mel_gpu_cmd_end_rendering(Mel_Gpu_Command_List* cmd)
{
    // D3D12 has no explicit end-of-rendering; the next OMSetRenderTargets or barrier supersedes this pass.
    mel_assert(cmd);
    (void)cmd;
}

// ---- U13: pipeline binding + draw/dispatch recording. cmd_bind_pipeline records the PSO + root signature +
// (for graphics) the IA topology, and — for a bindless pipeline — binds the two shader-visible heaps once so
// the shader can index ResourceDescriptorHeap / SamplerDescriptorHeap (the simple path: bind, push, draw). ----

void mel_gpu_cmd_bind_pipeline(Mel_Gpu_Command_List* cmd, Mel_Gpu_Pipeline pipe)
{
    Mel_Gpu_Pipeline_Obj* o = mel_gpu__table_get(cmd->dev, &cmd->dev->pipelines, pipe.slot);
    if (!o)
    {
        mel_assert(!"cmd_bind_pipeline: invalid pipeline handle");
        return;
    }
    cmd->cur_compute = o->is_compute;
    cmd->cur_bindless = o->bindless;
    cmd->cur_push_size = o->push_constant_size;
    cmd->cur_vertex_stride = o->vertex_stride;

    Mel_Gpu_Device* dev = cmd->dev;
    if (o->bindless)
        mel_gpu_cmd_bind_bindless(cmd); // SetDescriptorHeaps before the root signature / table binds

    if (o->is_compute)
        ID3D12GraphicsCommandList_SetComputeRootSignature(cmd->list, o->root_sig);
    else
    {
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(cmd->list, o->root_sig);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(cmd->list, o->topology);
    }

    // Bind the two bindless descriptor tables at the heap starts; the per-class range offsets in the root
    // signature map the shader's per-class index onto the right heap slot (binding.c).
    if (o->bindless && dev->bindless_enabled)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE srv, smp;
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(dev->srv_heap, &srv);
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(dev->smp_heap, &smp);
        if (o->is_compute)
        {
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cmd->list, o->srv_table_param, srv);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cmd->list, o->smp_table_param, smp);
        }
        else
        {
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cmd->list, o->srv_table_param, srv);
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cmd->list, o->smp_table_param, smp);
        }
    }
    ID3D12GraphicsCommandList_SetPipelineState(cmd->list, o->pso);
}

void mel_gpu_cmd_push_constants(Mel_Gpu_Command_List* cmd, u32 offset, u32 bytes, const void* data)
{
    mel_assert(cmd && cmd->cur_push_size > 0);
    UINT num = (bytes + 3) / 4;
    UINT dst = offset / 4;
    if (cmd->cur_compute)
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(cmd->list, 0, num, data, dst);
    else
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(cmd->list, 0, num, data, dst);
}

void mel_gpu_cmd_bind_vertex_buffer(Mel_Gpu_Command_List* cmd, u32 slot, Mel_Gpu_Buffer buf)
{
    Mel_Gpu_Buffer_Obj* o = NULL;
    if (!cmd || !mel_gpu__buffer_get(cmd->dev, buf, &o))
    {
        mel_assert(!"cmd_bind_vertex_buffer: invalid buffer handle");
        return;
    }
    D3D12_VERTEX_BUFFER_VIEW vbv = { .BufferLocation = o->gpu_va, .SizeInBytes = (UINT)o->size, .StrideInBytes = cmd->cur_vertex_stride };
    ID3D12GraphicsCommandList_IASetVertexBuffers(cmd->list, slot, 1, &vbv);
}

void mel_gpu_cmd_bind_index_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Index_Type type)
{
    Mel_Gpu_Buffer_Obj* o = NULL;
    if (!cmd || !mel_gpu__buffer_get(cmd->dev, buf, &o))
    {
        mel_assert(!"cmd_bind_index_buffer: invalid buffer handle");
        return;
    }
    D3D12_INDEX_BUFFER_VIEW ibv = { .BufferLocation = o->gpu_va, .SizeInBytes = (UINT)o->size, .Format = type == MEL_GPU_INDEX_UINT32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT };
    ID3D12GraphicsCommandList_IASetIndexBuffer(cmd->list, &ibv);
}

void mel_gpu_cmd_draw(Mel_Gpu_Command_List* cmd, u32 vertex_count, u32 instance_count)
{
    mel_assert(cmd);
    ID3D12GraphicsCommandList_DrawInstanced(cmd->list, vertex_count, instance_count ? instance_count : 1, 0, 0);
}

void mel_gpu_cmd_draw_indexed(Mel_Gpu_Command_List* cmd, u32 index_count, u32 instance_count)
{
    mel_assert(cmd);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(cmd->list, index_count, instance_count ? instance_count : 1, 0, 0, 0);
}

void mel_gpu_cmd_dispatch(Mel_Gpu_Command_List* cmd, u32 groups_x, u32 groups_y, u32 groups_z)
{
    mel_assert(cmd);
    ID3D12GraphicsCommandList_Dispatch(cmd->list, groups_x, groups_y, groups_z);
}
