#include "vk_backend.h"

#include <log/log.h>

static bool mel_gpu__query_pool_get(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool, Mel_Gpu_Query_Pool_Obj* out) { return mel_gpu__table_get_copy(dev, &dev->query_pools, pool.slot, out); }

Mel_Gpu_Query_Pool_Create_Result mel_gpu_query_pool_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool_Opt opt)
{
    Mel_Gpu_Query_Pool_Create_Result res = { .value = { mel_gpu_handle_null() }, .status = MEL_GPU_QUERY_POOL_CREATE_OK };

    if (!dev)
    {
        res.status = MEL_GPU_QUERY_POOL_CREATE_BAD_PARAMS;
        return res;
    }
    if (opt.count == 0)
    {
        mel_log_error("gpu", "query_pool_create: count must be nonzero");
        res.status = MEL_GPU_QUERY_POOL_CREATE_BAD_PARAMS;
        return res;
    }
    if (opt.type != MEL_GPU_QUERY_TIMESTAMP)
    {
        mel_log_error("gpu", "query_pool_create: query type %d not yet implemented (timestamp only)", (int)opt.type);
        res.status = MEL_GPU_QUERY_POOL_CREATE_BAD_PARAMS;
        return res;
    }
    if (dev->caps.queries.timestamp == MEL_GPU_TIMESTAMP_NONE || dev->caps.queries.timestamp_period_ns <= 0.0)
    {
        mel_log_error("gpu", "query_pool_create: timestamp queries unsupported on '%s' (timestamp_period_ns=%.3f)", dev->caps.adapter.name, dev->caps.queries.timestamp_period_ns);
        res.status = MEL_GPU_QUERY_POOL_CREATE_UNSUPPORTED;
        return res;
    }
    if (!dev->caps.queries.timestamp_compute_and_graphics)
    {
        mel_log_error("gpu", "query_pool_create: timestampComputeAndGraphics absent on '%s'; timestamp pool refused", dev->caps.adapter.name);
        res.status = MEL_GPU_QUERY_POOL_CREATE_UNSUPPORTED;
        return res;
    }

    VkQueryPoolCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = opt.count,
    };
    VkQueryPool vk = VK_NULL_HANDLE;
    VkResult    r = vkCreateQueryPool(dev->vk, &ci, NULL, &vk);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "query_pool_create: vkCreateQueryPool failed: %s", mel_gpu__vk_result_str(r));
        res.status = MEL_GPU_QUERY_POOL_CREATE_BACKEND_FAILED;
        return res;
    }

    Mel_Gpu_Query_Pool_Obj obj = { 0 };
    obj.header.ownership = MEL_GPU_OWNERSHIP_OWNED;
    obj.header.name = opt.name;
    obj.pool = vk;
    obj.type = opt.type;
    obj.count = opt.count;
    obj.period_ns = dev->caps.queries.timestamp_period_ns;
    res.value.slot = mel_gpu__table_insert(dev, &dev->query_pools, &obj);
    return res;
}

void mel_gpu_query_pool_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool)
{
    const void* trk = mel_gpu__track_key(&dev->query_pools, pool.slot.index);
    mel_gpu__track_enter(dev, trk, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    Mel_Gpu_Query_Pool_Obj o;
    if (!mel_gpu__table_get_copy(dev, &dev->query_pools, pool.slot, &o))
    {
        mel_gpu__track_exit(dev, trk);
        return;
    }
    VkQueryPool vk = o.pool;
    mel_gpu__table_remove(dev, &dev->query_pools, pool.slot);
    if (vk)
        vkDestroyQueryPool(dev->vk, vk, NULL);
    mel_gpu__track_exit(dev, trk);
}

bool mel_gpu_query_pool_alive(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool) { return mel_gpu__table_alive(dev, &dev->query_pools, pool.slot); }

void mel_gpu_cmd_reset_query_pool(Mel_Gpu_Command_List* cmd, Mel_Gpu_Query_Pool pool, u32 first, u32 count)
{
    Mel_Gpu_Query_Pool_Obj o;
    if (!cmd || !mel_gpu__query_pool_get(cmd->dev, pool, &o))
    {
        mel_assert(!"cmd_reset_query_pool: invalid query pool handle");
        return;
    }
    if (first + count > o.count)
    {
        mel_log_error("gpu", "cmd_reset_query_pool: range [%u, %u) exceeds pool count %u", first, first + count, o.count);
        mel_assert(!"cmd_reset_query_pool: range exceeds pool count");
        return;
    }
    vkCmdResetQueryPool(cmd->cb, o.pool, first, count);
}

void mel_gpu_cmd_write_timestamp(Mel_Gpu_Command_List* cmd, Mel_Gpu_Query_Pool pool, u32 index)
{
    Mel_Gpu_Query_Pool_Obj o;
    if (!cmd || !mel_gpu__query_pool_get(cmd->dev, pool, &o))
    {
        mel_assert(!"cmd_write_timestamp: invalid query pool handle");
        return;
    }
    if (index >= o.count)
    {
        mel_log_error("gpu", "cmd_write_timestamp: index %u exceeds pool count %u", index, o.count);
        mel_assert(!"cmd_write_timestamp: index exceeds pool count");
        return;
    }
    vkCmdWriteTimestamp(cmd->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, o.pool, index);
}

bool mel_gpu_query_pool_resolve(Mel_Gpu_Device* dev, Mel_Gpu_Query_Pool pool, u32 first, u32 count, u64* out_ns)
{
    Mel_Gpu_Query_Pool_Obj o;
    if (!dev || !out_ns || count == 0 || !mel_gpu__query_pool_get(dev, pool, &o))
    {
        mel_assert(!"query_pool_resolve: invalid arguments");
        return false;
    }
    if (first + count > o.count)
    {
        mel_log_error("gpu", "query_pool_resolve: range [%u, %u) exceeds pool count %u", first, first + count, o.count);
        mel_assert(!"query_pool_resolve: range exceeds pool count");
        return false;
    }

    u64*     ticks = mel_alloc_array(dev->alloc, u64, count);
    VkResult r = vkGetQueryPoolResults(dev->vk, o.pool, first, count, sizeof(u64) * count, ticks, sizeof(u64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (r != VK_SUCCESS)
    {
        mel_log_error("gpu", "query_pool_resolve: vkGetQueryPoolResults failed: %s", mel_gpu__vk_result_str(r));
        mel_dealloc(dev->alloc, ticks);
        return false;
    }

    for (u32 i = 0; i < count; i++)
        out_ns[i] = (u64)((f64)ticks[i] * o.period_ns);

    mel_dealloc(dev->alloc, ticks);
    return true;
}

typedef struct
{
    Mel_Gpu_Device*       dev;
    Mel_Gpu_Command_List* cmd;
    Mel_Gpu_Buffer        readback;
    Mel_Gpu_Future*       result_future;
    u32                   count;
    f64                   period_ns;
} Mel_Gpu_Query_Resolve_Ctx;

static void mel_gpu__query_resolve_complete(Mel_Gpu_Future* submit_future, void* user)
{
    Mel_Gpu_Query_Resolve_Ctx* c = user;
    Mel_Gpu_Device*            dev = c->dev;

    u32 submit_status = mel_gpu_future_status(submit_future);

    if (mel_gpu_failed(submit_status))
    {
        mel_log_error("gpu", "query_pool_resolve_async: resolve-copy submission failed; future resolves ERROR");
        mel_gpu_future_resolve(c->result_future, NULL, MEL_GPU_QUERY_RESOLVE_BACKEND_FAILED);
    }
    else
    {
        const u64*             ticks = mel_gpu_buffer_mapped(dev, c->readback);
        usize                  bytes = sizeof(Mel_Gpu_Query_Resolve) + (usize)c->count * sizeof(u64);
        u8*                    block = mel_alloc(dev->alloc, bytes);
        Mel_Gpu_Query_Resolve* out = (Mel_Gpu_Query_Resolve*)block;
        u64*                   ns = (u64*)(block + sizeof(Mel_Gpu_Query_Resolve));
        for (u32 i = 0; i < c->count; i++)
            ns[i] = ticks ? (u64)((f64)ticks[i] * c->period_ns) : 0;
        out->ns = ns;
        out->count = c->count;
        mel_gpu_future_resolve(c->result_future, out, ticks ? MEL_GPU_QUERY_RESOLVE_OK : MEL_GPU_QUERY_RESOLVE_BACKEND_FAILED);
    }

    mel_gpu_future_destroy(submit_future);
    mel_gpu_command_list_destroy(c->cmd);
    mel_gpu_buffer_destroy(dev, c->readback);
    mel_dealloc(dev->alloc, c);
}

Mel_Gpu_Future* mel_gpu_query_pool_resolve_async(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q, Mel_Gpu_Query_Pool pool, u32 first, u32 count)
{
    Mel_Gpu_Query_Pool_Obj o;
    if (!dev || !q || count == 0 || !mel_gpu__query_pool_get(dev, pool, &o))
    {
        mel_log_error("gpu", "query_pool_resolve_async: invalid arguments (dev/queue/count/pool)");
        Mel_Gpu_Future* f = mel_gpu_future_create(dev ? dev->pump : NULL, dev ? dev->vat : NULL);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_QUERY_RESOLVE_BAD_PARAMS);
        return f;
    }
    if (first + count > o.count)
    {
        mel_log_error("gpu", "query_pool_resolve_async: range [%u, %u) exceeds pool count %u", first, first + count, o.count);
        Mel_Gpu_Future* f = mel_gpu_future_create(dev->pump, dev->vat);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_QUERY_RESOLVE_BAD_PARAMS);
        return f;
    }

    Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = (usize)count * sizeof(u64), .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "query-resolve-readback");
    if (mel_gpu_failed(rb.status))
    {
        mel_log_error("gpu", "query_pool_resolve_async: readback buffer create failed");
        Mel_Gpu_Future* f = mel_gpu_future_create(dev->pump, dev->vat);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_QUERY_RESOLVE_BACKEND_FAILED);
        return f;
    }
    VkBuffer rb_vk = VK_NULL_HANDLE;
    mel_gpu__buffer_get(dev, rb.value, &rb_vk);

    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    if (!cmd)
    {
        mel_log_error("gpu", "query_pool_resolve_async: command list create failed");
        mel_gpu_buffer_destroy(dev, rb.value);
        Mel_Gpu_Future* f = mel_gpu_future_create(dev->pump, dev->vat);
        mel_gpu_future_resolve(f, NULL, MEL_GPU_QUERY_RESOLVE_BACKEND_FAILED);
        return f;
    }

    mel_gpu_command_list_begin(cmd);
    vkCmdCopyQueryPoolResults(cmd->cb, o.pool, first, count, rb_vk, 0, sizeof(u64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    mel_gpu_cmd_buffer_barrier(cmd, rb.value, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_COMMON);
    mel_gpu_command_list_end(cmd);

    Mel_Gpu_Query_Resolve_Ctx* c = mel_alloc_type(dev->alloc, Mel_Gpu_Query_Resolve_Ctx);
    *c = (Mel_Gpu_Query_Resolve_Ctx){ .dev = dev, .cmd = cmd, .readback = rb.value, .count = count, .period_ns = o.period_ns };
    c->result_future = mel_gpu_future_create(dev->pump, dev->vat);
    Mel_Gpu_Future* result_future = c->result_future;

    Mel_Gpu_Future* submit_future = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });

    if (mel_gpu_future_resolved(submit_future))
        mel_gpu__query_resolve_complete(submit_future, c);
    else
        mel_gpu_future_then(submit_future, mel_gpu__query_resolve_complete, c);

    return result_future;
}

void mel_gpu_query_resolve_future_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Future* f)
{
    if (!f)
        return;
    void* val = mel_gpu_future_value(f);
    if (val)
        mel_dealloc(dev->alloc, val);
    mel_gpu_future_destroy(f);
}
