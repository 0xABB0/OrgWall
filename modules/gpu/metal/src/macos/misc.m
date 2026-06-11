#include "mtl_backend.h"

#include <log/log.h>

u64 mel_gpu_buffer_device_address(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf)
{
    (void)dev;
    (void)buf;
    mel_log_error("gpu", "buffer_device_address: GPU virtual addresses are not exposed on the Metal backend this round");
    return 0;
}

Mel_Gpu_Format_Properties mel_gpu_format_properties(Mel_Gpu_Device* dev, Mel_Gpu_Format format, Mel_Gpu_Tiling tiling)
{
    Mel_Gpu_Format_Properties p = { 0 };
    if (!dev)
        return p;

    MTLPixelFormat mf = mel_gpu__mtl_format(format);
    if (mf == MTLPixelFormatInvalid)
        return p;

    u32 f = MEL_GPU_FMT_SAMPLED | MEL_GPU_FMT_BLIT_SRC | MEL_GPU_FMT_BLIT_DST | MEL_GPU_FMT_TRANSFER_SRC | MEL_GPU_FMT_TRANSFER_DST;
    if (!mel_gpu_format_is_depth(format))
    {
        f |= MEL_GPU_FMT_COLOR_ATTACHMENT | MEL_GPU_FMT_COLOR_BLEND | MEL_GPU_FMT_STORAGE | MEL_GPU_FMT_LINEAR_FILTER;
    }
    else
    {
        f |= MEL_GPU_FMT_DEPTH_ATTACHMENT;
    }

    if (tiling == MEL_GPU_TILING_LINEAR)
        p.linear_tiling_features = f & (MEL_GPU_FMT_SAMPLED | MEL_GPU_FMT_TRANSFER_SRC | MEL_GPU_FMT_TRANSFER_DST | MEL_GPU_FMT_LINEAR_FILTER);
    else
        p.tiling_features = f;

    p.buffer_features = MEL_GPU_FMT_VERTEX_BUFFER;
    p.sample_counts = 1u | 4u;
    return p;
}

Mel_Gpu_Sync_Create_Result mel_gpu_sync_create(Mel_Gpu_Device* dev, Mel_Gpu_Sync_Kind kind, u64 initial_value)
{
    (void)dev;
    (void)kind;
    (void)initial_value;
    mel_log_error("gpu", "sync_create: explicit sync primitives are not implemented on the Metal backend this round (use the per-submission completion future)");
    return (Mel_Gpu_Sync_Create_Result){ .value = { mel_gpu_handle_null() }, .status = MEL_GPU_SYNC_CREATE_UNSUPPORTED };
}

void mel_gpu_sync_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync)
{
    (void)dev;
    (void)sync;
}

bool mel_gpu_sync_alive(Mel_Gpu_Device* dev, Mel_Gpu_Sync sync)
{
    (void)dev;
    (void)sync;
    return false;
}

Mel_Gpu_Query_Pool_Create_Result mel_gpu_query_pool_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool_Opt opt)
{
    (void)dev;
    (void)opt;
    mel_log_error("gpu", "query_pool_create: timestamp queries are not implemented on the Metal backend this round (caps.queries.timestamp=none)");
    return (Mel_Gpu_Query_Pool_Create_Result){ .value = { mel_gpu_handle_null() }, .status = MEL_GPU_QUERY_POOL_CREATE_UNSUPPORTED };
}

void mel_gpu_query_pool_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool)
{
    (void)dev;
    (void)pool;
}

bool mel_gpu_query_pool_alive(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool)
{
    (void)dev;
    (void)pool;
    return false;
}

void mel_gpu_cmd_reset_query_pool(Mel_Gpu_Command_List* cmd, Mel_Gpu_Query_Pool pool, u32 first, u32 count)
{
    (void)cmd;
    (void)pool;
    (void)first;
    (void)count;
}

void mel_gpu_cmd_write_timestamp(Mel_Gpu_Command_List* cmd, Mel_Gpu_Query_Pool pool, u32 index)
{
    (void)cmd;
    (void)pool;
    (void)index;
}

bool mel_gpu_query_pool_resolve(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool, u32 first, u32 count, u64* out_ns)
{
    (void)dev;
    (void)pool;
    (void)first;
    (void)count;
    (void)out_ns;
    return false;
}

Mel_Gpu_Bind_Group_Layout mel_gpu_bind_group_layout_create(Mel_Gpu_Device* dev, const Mel_Gpu_Bind_Group_Layout_Entry* entries, u32 count)
{
    (void)dev;
    (void)entries;
    (void)count;
    mel_log_error("gpu", "bind_group_layout_create: descriptor sets are not implemented on the Metal backend this round");
    return (Mel_Gpu_Bind_Group_Layout){ mel_gpu_handle_null() };
}

void mel_gpu_bind_group_layout_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout)
{
    (void)dev;
    (void)layout;
}

bool mel_gpu_bind_group_layout_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout)
{
    (void)dev;
    (void)layout;
    return false;
}

Mel_Gpu_Bind_Group mel_gpu_bind_group_create(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout)
{
    (void)dev;
    (void)layout;
    mel_log_error("gpu", "bind_group_create: descriptor sets are not implemented on the Metal backend this round");
    return (Mel_Gpu_Bind_Group){ mel_gpu_handle_null() };
}

void mel_gpu_bind_group_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group)
{
    (void)dev;
    (void)group;
}

bool mel_gpu_bind_group_alive(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group)
{
    (void)dev;
    (void)group;
    return false;
}

void mel_gpu_bind_group_write_texture(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view)
{
    (void)dev;
    (void)group;
    (void)binding;
    (void)array_element;
    (void)view;
}

void mel_gpu_bind_group_write_sampler(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Sampler sampler)
{
    (void)dev;
    (void)group;
    (void)binding;
    (void)array_element;
    (void)sampler;
}

void mel_gpu_bind_group_write_combined(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Texture_View view, Mel_Gpu_Sampler sampler)
{
    (void)dev;
    (void)group;
    (void)binding;
    (void)array_element;
    (void)view;
    (void)sampler;
}

void mel_gpu_bind_group_write_buffer(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, u32 binding, u32 array_element, Mel_Gpu_Buffer buffer)
{
    (void)dev;
    (void)group;
    (void)binding;
    (void)array_element;
    (void)buffer;
}

void mel_gpu_cmd_bind_descriptor_set(Mel_Gpu_Command_List* cmd, u32 set_index, Mel_Gpu_Bind_Group group)
{
    (void)cmd;
    (void)set_index;
    (void)group;
    mel_log_warn("gpu", "cmd_bind_descriptor_set: descriptor sets are not implemented on the Metal backend this round");
}
