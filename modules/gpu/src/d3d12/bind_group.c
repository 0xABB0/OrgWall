#include "d3d_backend.h"

#include <log/log.h>

enum
{
    MEL_D3D12_CLASSIC_RES_CAP = 4096,
    MEL_D3D12_CLASSIC_SMP_CAP = 512,
};

bool mel_gpu__descriptor_is_sampler(Mel_Gpu_Descriptor_Kind kind) { return kind == MEL_GPU_DESCRIPTOR_SAMPLER; }

D3D12_DESCRIPTOR_RANGE_TYPE mel_gpu__range_type(Mel_Gpu_Descriptor_Kind kind)
{
    switch (kind)
    {
    case MEL_GPU_DESCRIPTOR_SAMPLER:
        return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    case MEL_GPU_DESCRIPTOR_STORAGE_IMAGE:
    case MEL_GPU_DESCRIPTOR_STORAGE_BUFFER:
        return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    case MEL_GPU_DESCRIPTOR_UNIFORM_BUFFER:
        return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case MEL_GPU_DESCRIPTOR_SAMPLED_IMAGE:
    case MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER:
    default:
        return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }
}

void mel_gpu__classic_init(Mel_Gpu_Device* dev)
{
    mel_mutex_init(&dev->classic_lock, MEL_MUTEX_PLAIN);
    dev->classic_res_cap = MEL_D3D12_CLASSIC_RES_CAP;
    dev->classic_smp_cap = MEL_D3D12_CLASSIC_SMP_CAP;
    D3D12_DESCRIPTOR_HEAP_DESC rd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = dev->classic_res_cap, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    D3D12_DESCRIPTOR_HEAP_DESC sd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, .NumDescriptors = dev->classic_smp_cap, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    HRESULT                    hr = ID3D12Device_CreateDescriptorHeap(dev->d3d, &rd, &IID_ID3D12DescriptorHeap, (void**)&dev->classic_res_heap);
    HRESULT                    hs = ID3D12Device_CreateDescriptorHeap(dev->d3d, &sd, &IID_ID3D12DescriptorHeap, (void**)&dev->classic_smp_heap);
    if (FAILED(hr) || FAILED(hs) || !dev->classic_res_heap || !dev->classic_smp_heap)
    {
        mel_log_error("gpu", "classic_init: shader-visible heap creation failed (res=0x%08lx smp=0x%08lx)", (unsigned long)hr, (unsigned long)hs);
        if (dev->classic_res_heap)
            ID3D12DescriptorHeap_Release(dev->classic_res_heap);
        if (dev->classic_smp_heap)
            ID3D12DescriptorHeap_Release(dev->classic_smp_heap);
        dev->classic_res_heap = dev->classic_smp_heap = NULL;
        return;
    }
    dev->classic_res_inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev->d3d, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dev->classic_smp_inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev->d3d, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void mel_gpu__classic_destroy(Mel_Gpu_Device* dev)
{
    if (dev->classic_res_heap)
        ID3D12DescriptorHeap_Release(dev->classic_res_heap);
    if (dev->classic_smp_heap)
        ID3D12DescriptorHeap_Release(dev->classic_smp_heap);
    if (dev->classic_res_heap || dev->classic_smp_heap)
        mel_mutex_destroy(&dev->classic_lock);
    if (dev->classic_res_free)
        mel_dealloc(dev->alloc, dev->classic_res_free);
    if (dev->classic_smp_free)
        mel_dealloc(dev->alloc, dev->classic_smp_free);
    dev->classic_res_free = dev->classic_smp_free = NULL;
    dev->classic_res_heap = dev->classic_smp_heap = NULL;
}

static bool mel_gpu__classic_alloc(u32 count, u32* free_count_ptr, Mel_Gpu_Classic_Block* free_list, u32* cursor, u32 cap, u32* out_base)
{
    if (count == 0)
    {
        *out_base = *cursor;
        return true;
    }
    u32 fc = *free_count_ptr;
    for (u32 i = 0; i < fc; i++)
    {
        if (free_list[i].count < count)
            continue;
        *out_base = free_list[i].base;
        if (free_list[i].count == count)
            free_list[i] = free_list[--fc];
        else
        {
            free_list[i].base += count;
            free_list[i].count -= count;
        }
        *free_count_ptr = fc;
        return true;
    }
    if (*cursor + count > cap)
        return false;
    *out_base = *cursor;
    *cursor += count;
    return true;
}

static void mel_gpu__classic_free_block(Mel_Gpu_Device* dev, u32* free_count_ptr, u32* free_cap_ptr, Mel_Gpu_Classic_Block** free_list_ptr, u32* cursor, Mel_Gpu_Classic_Block block)
{
    if (block.count == 0)
        return;

    Mel_Gpu_Classic_Block* fl = *free_list_ptr;
    u32                    fc = *free_count_ptr;
    bool                   coalesced = true;
    while (coalesced)
    {
        coalesced = false;
        for (u32 i = 0; i < fc; i++)
        {
            if (fl[i].base + fl[i].count == block.base)
            {
                block.base = fl[i].base;
                block.count += fl[i].count;
                fl[i] = fl[--fc];
                coalesced = true;
                break;
            }
            if (block.base + block.count == fl[i].base)
            {
                block.count += fl[i].count;
                fl[i] = fl[--fc];
                coalesced = true;
                break;
            }
        }
    }

    if (block.base + block.count == *cursor)
    {
        *cursor = block.base;
        bool retracted = true;
        while (retracted)
        {
            retracted = false;
            for (u32 i = 0; i < fc; i++)
            {
                if (fl[i].base + fl[i].count == *cursor)
                {
                    *cursor = fl[i].base;
                    fl[i] = fl[--fc];
                    retracted = true;
                    break;
                }
            }
        }
        *free_count_ptr = fc;
        return;
    }

    if (fc == *free_cap_ptr)
    {
        u32 cap = *free_cap_ptr ? *free_cap_ptr * 2 : 16;
        fl = fl ? mel_realloc(dev->alloc, fl, sizeof(Mel_Gpu_Classic_Block) * cap) : mel_alloc(dev->alloc, sizeof(Mel_Gpu_Classic_Block) * cap);
        *free_list_ptr = fl;
        *free_cap_ptr = cap;
    }
    fl[fc++] = block;
    *free_count_ptr = fc;
}

bool mel_gpu__classic_res_alloc(Mel_Gpu_Device* dev, u32 count, u32* out_base)
{
    mel_mutex_lock(&dev->classic_lock);
    bool ok = mel_gpu__classic_alloc(count, &dev->classic_res_free_count, dev->classic_res_free, &dev->classic_res_next, dev->classic_res_cap, out_base);
    mel_mutex_unlock(&dev->classic_lock);
    return ok;
}

bool mel_gpu__classic_smp_alloc(Mel_Gpu_Device* dev, u32 count, u32* out_base)
{
    mel_mutex_lock(&dev->classic_lock);
    bool ok = mel_gpu__classic_alloc(count, &dev->classic_smp_free_count, dev->classic_smp_free, &dev->classic_smp_next, dev->classic_smp_cap, out_base);
    mel_mutex_unlock(&dev->classic_lock);
    return ok;
}

void mel_gpu__classic_res_free(Mel_Gpu_Device* dev, Mel_Gpu_Classic_Block block)
{
    mel_mutex_lock(&dev->classic_lock);
    mel_gpu__classic_free_block(dev, &dev->classic_res_free_count, &dev->classic_res_free_cap, &dev->classic_res_free, &dev->classic_res_next, block);
    mel_mutex_unlock(&dev->classic_lock);
}

void mel_gpu__classic_smp_free(Mel_Gpu_Device* dev, Mel_Gpu_Classic_Block block)
{
    mel_mutex_lock(&dev->classic_lock);
    mel_gpu__classic_free_block(dev, &dev->classic_smp_free_count, &dev->classic_smp_free_cap, &dev->classic_smp_free, &dev->classic_smp_next, block);
    mel_mutex_unlock(&dev->classic_lock);
}

u32 mel_gpu__classic_res_in_use(Mel_Gpu_Device* dev)
{
    mel_mutex_lock(&dev->classic_lock);
    u32 free_total = 0;
    for (u32 i = 0; i < dev->classic_res_free_count; i++)
        free_total += dev->classic_res_free[i].count;
    u32 in_use = dev->classic_res_next - free_total;
    mel_mutex_unlock(&dev->classic_lock);
    return in_use;
}

u32 mel_gpu__classic_smp_in_use(Mel_Gpu_Device* dev)
{
    mel_mutex_lock(&dev->classic_lock);
    u32 free_total = 0;
    for (u32 i = 0; i < dev->classic_smp_free_count; i++)
        free_total += dev->classic_smp_free[i].count;
    u32 in_use = dev->classic_smp_next - free_total;
    mel_mutex_unlock(&dev->classic_lock);
    return in_use;
}

D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__classic_res_cpu(Mel_Gpu_Device* dev, u32 slot)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dev->classic_res_heap, &h);
    h.ptr += (SIZE_T)slot * dev->classic_res_inc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE mel_gpu__classic_res_gpu(Mel_Gpu_Device* dev, u32 slot)
{
    D3D12_GPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(dev->classic_res_heap, &h);
    h.ptr += (UINT64)slot * dev->classic_res_inc;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__classic_smp_cpu(Mel_Gpu_Device* dev, u32 slot)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dev->classic_smp_heap, &h);
    h.ptr += (SIZE_T)slot * dev->classic_smp_inc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE mel_gpu__classic_smp_gpu(Mel_Gpu_Device* dev, u32 slot)
{
    D3D12_GPU_DESCRIPTOR_HANDLE h;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(dev->classic_smp_heap, &h);
    h.ptr += (UINT64)slot * dev->classic_smp_inc;
    return h;
}

bool mel_gpu__bind_group_layout_get(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout, Mel_Gpu_Bind_Group_Layout_Obj** out)
{
    Mel_Gpu_Bind_Group_Layout_Obj* o = mel_gpu__table_get(dev, &dev->bind_group_layouts, layout.slot);
    if (!o)
        return false;
    *out = o;
    return true;
}

Mel_Gpu_Bind_Group_Layout mel_gpu_bind_group_layout_create(Mel_Gpu_Device* dev, const Mel_Gpu_Bind_Group_Layout_Entry* entries, u32 count)
{
    Mel_Gpu_Bind_Group_Layout h = { mel_gpu_handle_null() };
    if (!dev || (!entries && count))
    {
        mel_assert(!"bind_group_layout_create: null entries");
        return h;
    }

    Mel_Gpu_Bind_Group_Layout_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.entry_count = count;
    if (count)
    {
        obj.entries = mel_alloc(dev->alloc, sizeof(Mel_Gpu_Bind_Group_Layout_Entry) * count);
        for (u32 i = 0; i < count; i++)
        {
            obj.entries[i] = entries[i];
            u32 n = entries[i].count ? entries[i].count : 1u;
            if (mel_gpu__descriptor_is_sampler(entries[i].kind))
                obj.sampler_descriptor_count += n;
            else
                obj.resource_descriptor_count += n;
        }
    }
    h.slot = mel_gpu__table_insert(dev, &dev->bind_group_layouts, &obj);
    return h;
}

void mel_gpu_bind_group_layout_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout)
{
    Mel_Gpu_Bind_Group_Layout_Obj* o = mel_gpu__table_get(dev, &dev->bind_group_layouts, layout.slot);
    if (!o)
        return;
    Mel_Gpu_Bind_Group_Layout_Entry* entries = o->entries;
    mel_gpu__table_remove(dev, &dev->bind_group_layouts, layout.slot);
    if (entries)
        mel_dealloc(dev->alloc, entries);
}

bool mel_gpu_bind_group_layout_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout) { return mel_gpu__table_get(dev, &dev->bind_group_layouts, layout.slot) != NULL; }

Mel_Gpu_Bind_Group mel_gpu_bind_group_create(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout)
{
    Mel_Gpu_Bind_Group             h = { mel_gpu_handle_null() };
    Mel_Gpu_Bind_Group_Layout_Obj* lo = NULL;
    if (!dev || !dev->classic_res_heap || !mel_gpu__bind_group_layout_get(dev, layout, &lo))
    {
        mel_assert(!"bind_group_create: invalid layout handle or no classic heap");
        return h;
    }

    u32 res_n = lo->resource_descriptor_count;
    u32 smp_n = lo->sampler_descriptor_count;

    u32 res_base = 0;
    u32 smp_base = 0;
    if (!mel_gpu__classic_res_alloc(dev, res_n, &res_base))
    {
        mel_log_error("gpu", "bind_group_create: classic resource heap exhausted (need %u, in-use %u/%u)", res_n, mel_gpu__classic_res_in_use(dev), dev->classic_res_cap);
        return h;
    }
    if (!mel_gpu__classic_smp_alloc(dev, smp_n, &smp_base))
    {
        mel_gpu__classic_res_free(dev, (Mel_Gpu_Classic_Block){ .base = res_base, .count = res_n });
        mel_log_error("gpu", "bind_group_create: classic sampler heap exhausted (need %u, in-use %u/%u)", smp_n, mel_gpu__classic_smp_in_use(dev), dev->classic_smp_cap);
        return h;
    }

    Mel_Gpu_Bind_Group_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.layout = layout.slot;
    obj.resource_base = res_base;
    obj.resource_count = res_n;
    obj.sampler_base = smp_base;
    obj.sampler_count = smp_n;
    h.slot = mel_gpu__table_insert(dev, &dev->bind_groups, &obj);
    return h;
}

void mel_gpu_bind_group_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group)
{
    Mel_Gpu_Bind_Group_Obj* g = mel_gpu__table_get(dev, &dev->bind_groups, group.slot);
    if (!g)
        return;
    Mel_Gpu_Classic_Block res = { .base = g->resource_base, .count = g->resource_count };
    Mel_Gpu_Classic_Block smp = { .base = g->sampler_base, .count = g->sampler_count };
    mel_gpu__table_remove(dev, &dev->bind_groups, group.slot);
    mel_gpu__defer_free(dev, (Mel_Gpu_Deferred_Free){ .classic_res = res, .has_classic_res = res.count > 0, .classic_smp = smp, .has_classic_smp = smp.count > 0 });
}

bool mel_gpu_bind_group_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group) { return mel_gpu__table_get(dev, &dev->bind_groups, group.slot) != NULL; }

static bool mel_gpu__bg_slot(Mel_Gpu_Device* dev, const Mel_Gpu_Bind_Group_Obj* g, u32 binding, u32 array_element, bool want_sampler, Mel_Gpu_Descriptor_Kind* out_kind, u32* out_slot)
{
    Mel_Gpu_Bind_Group_Layout_Obj* lo = mel_gpu__table_get(dev, &dev->bind_group_layouts, g->layout);
    if (!lo)
    {
        mel_assert(!"bind_group write: source layout was destroyed before the group");
        return false;
    }
    u32 res_off = 0;
    u32 smp_off = 0;
    for (u32 i = 0; i < lo->entry_count; i++)
    {
        u32  n = lo->entries[i].count ? lo->entries[i].count : 1u;
        bool is_smp = mel_gpu__descriptor_is_sampler(lo->entries[i].kind);
        if (lo->entries[i].binding == binding && is_smp == want_sampler)
        {
            if (array_element >= n)
            {
                mel_log_error("gpu", "bind_group write: array_element %u out of range for binding %u (count %u)", array_element, binding, n);
                mel_assert(!"bind_group write: array element out of range");
                return false;
            }
            *out_kind = lo->entries[i].kind;
            *out_slot = (is_smp ? g->sampler_base + smp_off : g->resource_base + res_off) + array_element;
            return true;
        }
        if (is_smp)
            smp_off += n;
        else
            res_off += n;
    }
    mel_log_error("gpu", "bind_group write: %s binding %u is not declared by the group's layout", want_sampler ? "sampler" : "resource", binding);
    mel_assert(!"bind_group write: undeclared binding");
    return false;
}

void mel_gpu_bind_group_write_texture(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view)
{
    Mel_Gpu_Bind_Group_Obj*   g = NULL;
    Mel_Gpu_Texture_View_Obj* vo = NULL;
    Mel_Gpu_Descriptor_Kind   kind;
    u32                       res_slot;
    if (!(g = mel_gpu__table_get(dev, &dev->bind_groups, group.slot)) || !mel_gpu__texture_view_get(dev, view, &vo) || !mel_gpu__bg_slot(dev, g, binding, array_element, false, &kind, &res_slot))
        return;
    Mel_Gpu_Texture      tex = { vo->texture };
    Mel_Gpu_Texture_Obj* t = NULL;
    if (!mel_gpu__texture_get(dev, tex, &t))
        return;

    if (kind == MEL_GPU_DESCRIPTOR_STORAGE_IMAGE)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { .Format = vo->format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D };
        uav.Texture2D.MipSlice = vo->base_mip;
        ID3D12Device_CreateUnorderedAccessView(dev->d3d, t->resource, NULL, &uav, mel_gpu__classic_res_cpu(dev, res_slot));
    }
    else
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = { .Format = vo->format, .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D, .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
        srv.Texture2D.MostDetailedMip = vo->base_mip;
        srv.Texture2D.MipLevels = vo->mip_count;
        ID3D12Device_CreateShaderResourceView(dev->d3d, t->resource, &srv, mel_gpu__classic_res_cpu(dev, res_slot));
    }
}

void mel_gpu_bind_group_write_sampler(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Sampler sampler)
{
    Mel_Gpu_Bind_Group_Obj* g = NULL;
    Mel_Gpu_Descriptor_Kind kind;
    u32                     smp_slot;
    D3D12_SAMPLER_DESC      sd = { 0 };
    if (!(g = mel_gpu__table_get(dev, &dev->bind_groups, group.slot)) || !mel_gpu__sampler_desc(dev, sampler, &sd) || !mel_gpu__bg_slot(dev, g, binding, array_element, true, &kind, &smp_slot))
        return;
    ID3D12Device_CreateSampler(dev->d3d, &sd, mel_gpu__classic_smp_cpu(dev, smp_slot));
}

void mel_gpu_bind_group_write_combined(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view, Mel_Gpu_Sampler sampler)
{
    mel_log_error("gpu", "bind_group_write_combined: D3D12 has no combined image-sampler; declare a separate SAMPLED_IMAGE and SAMPLER binding and write each");
    (void)dev;
    (void)group;
    (void)binding;
    (void)array_element;
    (void)view;
    (void)sampler;
}

void mel_gpu_bind_group_write_buffer(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Buffer buffer)
{
    Mel_Gpu_Bind_Group_Obj* g = NULL;
    Mel_Gpu_Buffer_Obj*     b = NULL;
    Mel_Gpu_Descriptor_Kind kind;
    u32                     res_slot;
    if (!(g = mel_gpu__table_get(dev, &dev->bind_groups, group.slot)) || !mel_gpu__buffer_get(dev, buffer, &b) || !mel_gpu__bg_slot(dev, g, binding, array_element, false, &kind, &res_slot))
        return;

    if (kind == MEL_GPU_DESCRIPTOR_STORAGE_BUFFER)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = { .Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D12_UAV_DIMENSION_BUFFER };
        uav.Buffer.NumElements = (UINT)(b->size / 4);
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        ID3D12Device_CreateUnorderedAccessView(dev->d3d, b->resource, NULL, &uav, mel_gpu__classic_res_cpu(dev, res_slot));
    }
    else
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = { .BufferLocation = b->gpu_va, .SizeInBytes = (UINT)((b->size + 255) & ~(u64)255) };
        ID3D12Device_CreateConstantBufferView(dev->d3d, &cbv, mel_gpu__classic_res_cpu(dev, res_slot));
    }
}

void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Command_List* cmd, u32 set_index, Mel_Gpu_Bind_Group group)
{
    mel_assert(cmd);
    Mel_Gpu_Device*         dev = cmd->dev;
    Mel_Gpu_Bind_Group_Obj* g = mel_gpu__table_get(dev, &dev->bind_groups, group.slot);
    if (!g)
    {
        mel_assert(!"cmd_bind_descriptor_set: invalid bind group");
        return;
    }
    Mel_Gpu_Pipeline_Obj* p = cmd->cur_pipeline;
    if (!p || set_index >= p->set_param_count)
    {
        mel_assert(!"cmd_bind_descriptor_set: bind a pipeline with set_layouts first, or set_index out of range");
        return;
    }

    if (!cmd->classic_heaps_bound)
    {
        ID3D12DescriptorHeap* heaps[2] = { dev->classic_res_heap, dev->classic_smp_heap };
        ID3D12GraphicsCommandList_SetDescriptorHeaps(cmd->list, 2, heaps);
        cmd->classic_heaps_bound = true;
    }

    Mel_Gpu_Set_Param sp = p->set_params[set_index];
    if (sp.has_resource)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = mel_gpu__classic_res_gpu(dev, g->resource_base);
        if (cmd->cur_compute)
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cmd->list, sp.resource_param, h);
        else
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cmd->list, sp.resource_param, h);
    }
    if (sp.has_sampler)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = mel_gpu__classic_smp_gpu(dev, g->sampler_base);
        if (cmd->cur_compute)
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(cmd->list, sp.sampler_param, h);
        else
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(cmd->list, sp.sampler_param, h);
    }
}
