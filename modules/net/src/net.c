#include "net_internal.h"
#include "net_backend.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <collection/list.h>
#include <log/log.h>
#include <time/nano.h>

#include <assert.h>
#include <string.h>

static Mel_Future_Status net_future_status_from(Mel_Net_Status status)
{
    Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
    if (status & MEL_NET_TIMED_OUT)
        fs |= MEL_FUTURE_TIMED_OUT;
    return fs;
}

static void net_src_wakeables(Mel_Vat_Source* s, Mel_Vat_Wakeable** out, usize* count)
{
    Mel_Net_Op_Record* op = (Mel_Net_Op_Record*)mel_vat_source_state(s);
    *out = &op->wakeable;
    *count = 1;
}

static i64 net_src_deadline(Mel_Vat_Source* s)
{
    Mel_Net_Op_Record* op = (Mel_Net_Op_Record*)mel_vat_source_state(s);
    return op->deadline;
}

static bool net_src_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    Mel_Net_Op_Record* op = (Mel_Net_Op_Record*)mel_vat_source_state(s);
    op->wakeable.revents = 0;

    if (!op->on_event(op, false))
        return false;
    if (op->deadline != MEL_VAT_NEVER && (i64)mel_nanos_since_unspecified_epoch() >= op->deadline)
        op->on_event(op, true);
    return false;
}

static const Mel_Vat_Source_Vtbl NET_SRC_VT = {
    .wakeables = net_src_wakeables,
    .deadline = net_src_deadline,
    .drain = net_src_drain,
    .cancel = NULL,
};

Mel_Net_Op_Record* mel_net__op_begin(Mel_Net* net, Mel_Executor* deliver, Mel_Net_Op* out_op)
{
    const Mel_Alloc*   alloc = net->alloc;
    Mel_Net_Op_Record* op = mel_alloc_type(alloc, Mel_Net_Op_Record);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);

    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;

    op->net = net;
    op->alloc = alloc;
    op->deliver = deliver ? deliver : net->executor;
    op->fd = -1;
    op->deadline = MEL_VAT_NEVER;
    op->status_slot = &op->result.conn.status;
    op->oserr_slot = &op->result.conn.os_error;

    Mel_Net_Op_Record* slot = op;
    op->self = mel_slotmap_insert(&net->ops, &slot);
    if (out_op)
        *out_op = (Mel_Net_Op){ .index = op->self.index, .generation = op->self.generation };
    return op;
}

void mel_net__op_arm(Mel_Net_Op_Record* op, i32 fd, u32 events, i64 deadline)
{
    op->wakeable.handle = fd;
    op->wakeable.events = events;
    op->wakeable.revents = 0;
    op->deadline = deadline;
    op->source = mel_vat_source_open(op->net->vat, &NET_SRC_VT, op);
    op->armed = true;
}

void mel_net__op_settle(Mel_Net_Op_Record* op, Mel_Net_Status status, i32 os_error)
{
    if (op->settled)
        return;
    op->settled = true;

    *op->status_slot = status;
    *op->oserr_slot = os_error;

    if (op->armed)
    {
        op->armed = false;
        mel_vat_source_close(op->source);
        op->source = NULL;
    }
    if (op->owns_fd && op->fd >= 0)
    {
        mel_net__backend_close(op->fd);
        op->fd = -1;
    }
    if (op->on_settle)
        op->on_settle(op);

    if (!op->detached)
    {
        op->detached = true;
        mel_slotmap_remove(&op->net->ops, op->self);
    }

    if (status & MEL_NET_CANCELLED)
        mel_future_cancel(&op->future);
    else
        mel_future_resolve(&op->future, &op->result, net_future_status_from(status));
}

Mel_Future* mel_net__op_fail(Mel_Net* net, Mel_Net_Op_Record* op, Mel_Net_Status status, i32 os_error)
{
    (void)net;
    mel_net__op_settle(op, status, os_error);
    return &op->future;
}

void mel_net__op_free_record(Mel_Net_Op_Record* op)
{
    if (op->free_payload)
        op->free_payload(op);
    mel_dealloc(op->alloc, op);
}

void mel_net_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Mel_Net_Op_Record* op = mel_container_of(f, Mel_Net_Op_Record, future);
    mel_net__op_free_record(op);
}

static void net_finalize(Mel_Net* net)
{
    if (net->workers)
        mel_sem_destroy(&net->queue_items);
    mel_slotmap_free(&net->ops);
    const Mel_Alloc* alloc = net->alloc;
    if (net->workers)
        mel_dealloc(alloc, net->workers);
    mel_dealloc(alloc, net);
}

static void resolve_completion_run(Mel_Task* self)
{
    Mel_Net_Op_Record* op = mel_container_of(self, Mel_Net_Op_Record, completion_task);
    Mel_Net*           net = op->net;
    bool               reclaim = net->destroying && atomic_load_explicit(&op->future.cont, memory_order_acquire) == NULL;

    if (op->cancel_requested)
        mel_net__op_settle(op, MEL_NET_ERROR | MEL_NET_CANCELLED, 0);
    else
        mel_net__op_settle(op, op->result.resolve.status, op->result.resolve.os_error);
    if (reclaim)
        mel_net__op_free_record(op);

    mel_vat_release(net->vat);
    net->pending_posts--;
    if (net->destroying && net->pending_posts == 0)
        net_finalize(net);
}

static void resolve_job_run(Mel_Net_Op_Record* op)
{
    const char* host = str8_to_cstr_alloc(op->host, op->alloc);
    if (!host)
    {
        op->result.resolve.status = MEL_NET_ERROR;
        return;
    }
    op->result.resolve.status = mel_net__backend_resolve(host, op->rport, op->v4_only, op->v6_only, op->alloc, &op->result.resolve.items, &op->result.resolve.count, &op->result.resolve.os_error);
    mel_dealloc(op->alloc, (void*)host);
}

static int net_worker_main(void* user)
{
    Mel_Net* net = (Mel_Net*)user;
    for (;;)
    {
        mel_sem_wait(&net->queue_items);
        if (atomic_load_explicit(&net->running, memory_order_acquire) == 0)
            break;

        Mel_Mpsc_Node* node = mel_mpsc_pop(&net->queue);
        if (!node)
            continue;

        Mel_Net_Op_Record* op = mel_container_of(node, Mel_Net_Op_Record, queue_node);
        if (!op->cancel_requested)
            resolve_job_run(op);
        mel_vat_post(net->vat, &op->completion_task);
    }
    return 0;
}

Mel_Future* mel_net__resolve_submit(Mel_Net_Op_Record* op)
{
    Mel_Net* net = op->net;
    op->worker_op = true;
    op->submitted = true;
    net->pending_posts++;
    mel_task_init(&op->completion_task, resolve_completion_run);
    mel_vat_retain(net->vat);
    if (net->worker_count == 0)
    {
        resolve_job_run(op);
        mel_vat_post(net->vat, &op->completion_task);
        return &op->future;
    }
    mel_mpsc_push(&net->queue, &op->queue_node);
    mel_sem_post(&net->queue_items);
    return &op->future;
}

Mel_Net* mel_net_create_opt(Mel_Net_Opt opt)
{
    if (!opt.vat)
    {
        mel_log_error("net", "create: vat is required");
        return NULL;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Net*         net = mel_alloc_type(alloc, Mel_Net);
    if (!net)
        return NULL;
    memset(net, 0, sizeof *net);

    net->vat = opt.vat;
    net->executor = mel_vat_executor(opt.vat);
    net->alloc = alloc;
    net->backend_ready = mel_net__backend_available();
    if (!net->backend_ready)
        mel_log_warn("net", "no socket backend on this platform; ops will fail unavailable");

    mel_slotmap_init(&net->ops, alloc, .item_size = sizeof(Mel_Net_Op_Record*), .initial_capacity = 16);

    net->worker_count = opt.resolver_workers;
    if (net->worker_count == 0)
    {
        net->worker_count = 1;
        mel_log_info("net", "create: resolver_workers not set; using 1");
    }

    mel_mpsc_init(&net->queue);
    if (!mel_sem_init(&net->queue_items, 0))
    {
        mel_log_error("net", "create: semaphore init failed");
        mel_slotmap_free(&net->ops);
        mel_dealloc(alloc, net);
        return NULL;
    }

    atomic_store_explicit(&net->running, 1, memory_order_release);
    net->workers = mel_alloc_array(alloc, Mel_Thread, net->worker_count);
    if (!net->workers)
    {
        mel_sem_destroy(&net->queue_items);
        mel_slotmap_free(&net->ops);
        mel_dealloc(alloc, net);
        return NULL;
    }

    u32 spawned = 0;
    for (u32 i = 0; i < net->worker_count; i++)
    {
        if (!mel_thread_spawn(&net->workers[i], net_worker_main, net, .name = "mel-net-resolve"))
        {
            mel_log_warn("net", "create: failed to spawn resolver worker %u/%u", i, net->worker_count);
            break;
        }
        spawned++;
    }
    net->worker_count = spawned;
    if (spawned == 0)
    {
        atomic_store_explicit(&net->running, 0, memory_order_release);
        mel_dealloc(alloc, net->workers);
        net->workers = NULL;
        mel_log_warn("net", "create: no resolver workers available; resolving inline on the loop thread");
    }

    return net;
}

void mel_net_destroy(Mel_Net* net)
{
    if (!net)
        return;
    assert(mel_vat_is_owner(net->vat));

    Mel_Array(Mel_Net_Op_Record*) snap;
    mel_array_init(&snap, net->alloc);
    Mel_Net_Op_Record** data = (Mel_Net_Op_Record**)mel_slotmap_data(&net->ops);
    u32                 n = mel_slotmap_count(&net->ops);
    for (u32 i = 0; i < n; i++)
        mel_array_push(&snap, data[i]);
    for (usize i = 0; i < snap.count; i++)
    {
        Mel_Net_Op_Record* op = snap.items[i];
        if (op->worker_op)
            op->cancel_requested = true;
        else
            mel_net__op_settle(op, MEL_NET_ERROR | MEL_NET_CANCELLED, 0);
    }
    mel_array_free(&snap);

    atomic_store_explicit(&net->running, 0, memory_order_release);
    for (u32 i = 0; i < net->worker_count; i++)
        mel_sem_post(&net->queue_items);
    for (u32 i = 0; i < net->worker_count; i++)
        mel_thread_join(&net->workers[i], NULL);

    for (;;)
    {
        Mel_Mpsc_Node* node = mel_mpsc_pop(&net->queue);
        if (!node)
            break;
        Mel_Net_Op_Record* op = mel_container_of(node, Mel_Net_Op_Record, queue_node);
        bool               has_cont = atomic_load_explicit(&op->future.cont, memory_order_acquire) != NULL;
        mel_net__op_settle(op, MEL_NET_ERROR | MEL_NET_CANCELLED, 0);
        mel_vat_release(net->vat);
        net->pending_posts--;
        if (!has_cont)
            mel_net__op_free_record(op);
    }

    if (net->pending_posts == 0)
    {
        net_finalize(net);
        return;
    }
    net->destroying = true;
}

bool          mel_net_available(const Mel_Net* net) { return net && net->backend_ready; }
Mel_Vat*      mel_net_vat(const Mel_Net* net) { return net ? net->vat : NULL; }
Mel_Executor* mel_net_executor(const Mel_Net* net) { return net ? net->executor : NULL; }
u32           mel_net_pending(const Mel_Net* net) { return net ? mel_slotmap_count((Mel_SlotMap*)&net->ops) : 0; }

bool mel_net_cancel(Mel_Net* net, Mel_Net_Op handle)
{
    if (!net)
        return false;
    assert(mel_vat_is_owner(net->vat));
    Mel_SlotMap_Handle  h = mel_slotmap_handle_make(handle.index, handle.generation);
    Mel_Net_Op_Record** pp = (Mel_Net_Op_Record**)mel_slotmap_get(&net->ops, h);
    Mel_Net_Op_Record*  op = pp ? *pp : NULL;
    if (!op || op->settled)
        return false;
    if (op->worker_op)
    {
        op->cancel_requested = true;
        return true;
    }
    mel_net__op_settle(op, MEL_NET_ERROR | MEL_NET_CANCELLED, 0);
    return true;
}
